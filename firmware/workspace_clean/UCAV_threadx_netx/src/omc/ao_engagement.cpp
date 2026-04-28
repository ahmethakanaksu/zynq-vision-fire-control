/*
 * ao_engagement.cpp -- Engagement Active Object implementation.
 *
 * Two-state HSM (Disabled / Active). Active handles two trigger signals:
 *   TARGET_VERIFIED_SIG       -- single-target fire decision
 *   CLUSTER_FIRE_DECISION_SIG -- cluster-mode fire decision
 *
 * QP priority 6, queue 16 events, 4 KB ThreadX stack.
 *
 * No dedicated event pool: TargetVerifiedEvent and ClusterFireDecisionEvent
 * are small enough (~12 B each) for QP to draw them from the existing
 * LsrResultEvent pool (76-byte slots), so the cluster-mode addition costs
 * zero extra pool footprint.
 */

#include "ao_engagement.hpp"
#include "events.hpp"
#include "signals.hpp"
#include "udp_uplink.hpp"
#include "log.hpp"
#include "track_table.hpp"
#include "missile_inventory.hpp"
#include "cluster_table.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include <cstdio>
#include <cstdint>

namespace omc {
namespace {

/* ------------------------------------------------------------------ */
/* AO storage                                                         */
/* ------------------------------------------------------------------ */

constexpr unsigned ENG_QP_PRIO    = 6U;
constexpr unsigned ENG_QUEUE_LEN  = 16U;
constexpr unsigned ENG_STACK_SIZE = 4096U;

constexpr int FIRE_LINE_BUF_SIZE = 128;

struct Engagement {
    QActive super;
    int     fire_count;     /* total FIRE messages sent */
    int     skip_count;     /* targets seen but no missile available */
    int     cfire_count;    /* total CFIRE messages sent (cluster mode) */
    int     cskip_count;    /* clusters considered but no missile available */
};

Engagement g_eng;

QEvtPtr  g_eng_queue_storage[ENG_QUEUE_LEN];
uint8_t  g_eng_stack[ENG_STACK_SIZE] __attribute__((aligned(8)));

/* ------------------------------------------------------------------ */
/* HSM state function forward declarations                            */
/* ------------------------------------------------------------------ */

QState eng_initial (Engagement * const me, QEvt const * const e);
QState eng_disabled(Engagement * const me, QEvt const * const e);
QState eng_active  (Engagement * const me, QEvt const * const e);

/* ------------------------------------------------------------------ */
/* Engagement step (single-target) -- invoked from inside eng_active  */
/* ------------------------------------------------------------------ */

/* Single-target engagement step. The trigger is one freshly verified
 * hostile track. The strategy is straightforward:
 *   - one verified hostile -> one missile, picked by the class
 *     preference table (Tank/Zpt prefer MAM-L, Truck prefers Cirit;
 *     SOM is the last-resort heavy round).
 *   - if the preferred type is out, walk the table and take whatever
 *     is left for that class.
 *   - civilian / unknown is rejected up in missile_inventory_allocate
 *     so the engagement is silently skipped. */
void process_target(Engagement * const me, TargetVerifiedEvent const * tev)
{
    const VehicleClass cls = static_cast<VehicleClass>(tev->cls);

    /* Allocate a missile per the class preference table. */
    const int missile_id = missile_inventory_allocate_for_class(cls);
    if (missile_id < 0) {
        me->skip_count++;
        OMC_LOG("[OMC AO_ENG] SKIP track #%d cls=%d (no missile available, skip=%d)\r\n",
                tev->track_id, tev->cls, me->skip_count);
        return;
    }

    /* Mark the track engaged in the shared table. */
    (void)track_table_mark_engaged(tev->track_id);

    /* Format and ship the FIRE wire message:
     *   FIRE,<frame_id>,<track_id>,<class>,<missile_id> */
    char line[FIRE_LINE_BUF_SIZE];
    const int n = std::snprintf(line, sizeof(line),
                                "FIRE,%d,%03d,%d,%02d",
                                tev->frame_id, tev->track_id,
                                tev->cls, missile_id);
    if (n < 0 || n >= static_cast<int>(sizeof(line))) {
        OMC_LOG("[OMC AO_ENG] FIRE format overflow\r\n");
        return;
    }

    if (udp_uplink_send_line(line)) {
        me->fire_count++;
        OMC_LOG("[OMC AO_ENG] FIRE #%d : %s\r\n", me->fire_count, line);
    } else {
        OMC_LOG("[OMC AO_ENG] FIRE drop (uplink failure): %s\r\n", line);
    }

    /* Quick inventory snapshot so the operator can see how many rounds
     * are left after this engagement. */
    MissileInventoryStats s = missile_inventory_stats();
    OMC_LOG("    >> remaining inventory: Cirit=%d MAM-L=%d SOM=%d (total=%d)\r\n",
            s.cirit_available, s.maml_available, s.som_available,
            s.total_available);
}

/* Cluster fire decision.
 *
 * Triggered by ClusterFireDecisionEvent posted by AO_LsrManager after a
 * CLSRRES batch is applied. The pipeline:
 *   1. Pick the highest-scoring non-engaged verified cluster.
 *   2. Determine has_tank from the member track_id encoding (0..99 = Tank).
 *   3. Allocate a missile via the cluster preference table.
 *   4. If no missile is available, skip silently with a counter bump --
 *      the cluster stays unengaged and is eligible on the next pulse.
 *   5. Mark the cluster engaged, mark each member track engaged.
 *   6. Emit one CFIRE wire line:
 *        CFIRE,<decision_frame>,<cluster_id>,<cluster_score>,<missile_id>
 *
 * The handler is idempotent: when no eligible verified cluster exists it
 * returns silently. That keeps the contract with AO_LsrManager simple
 * (one CLSRRES batch always emits one decision pulse, regardless of
 * outcome). */
void process_cluster_decision(Engagement * const me, ClusterFireDecisionEvent const * dev)
{
    VerifiedCluster best;
    if (!cluster_table_peek_best_verified(best)) {
        return; /* no eligible verified cluster: silent no-op */
    }

    /* has_tank check by track_id encoding: Tanks are issued track_ids in
     * [0..99] (idx*100+serial with idx=0). Cheaper than a track_table
     * lookup and exact for our deterministic ID scheme. */
    bool has_tank = false;
    for (int i = 0; i < best.member_count; ++i) {
        const int tid = best.track_ids[i];
        if (tid >= 0 && tid < 100) {
            has_tank = true;
            break;
        }
    }

    const int missile_id =
        missile_inventory_allocate_for_cluster(best.score, has_tank);
    if (missile_id < 0) {
        me->cskip_count++;
        OMC_LOG("[OMC AO_ENG] CSKIP cluster #%d score=%d (no missile, cskip=%d)\r\n",
                best.cluster_id, best.score, me->cskip_count);
        return;
    }

    (void)cluster_table_mark_cluster_engaged(best.cluster_id);
    for (int i = 0; i < best.member_count; ++i) {
        (void)track_table_mark_engaged(best.track_ids[i]);
    }

    char line[FIRE_LINE_BUF_SIZE];
    const int n = std::snprintf(line, sizeof(line),
                                "CFIRE,%d,%d,%d,%02d",
                                dev->frame_id,
                                best.cluster_id,
                                best.score,
                                missile_id);
    if (n < 0 || n >= static_cast<int>(sizeof(line))) {
        OMC_LOG("[OMC AO_ENG] CFIRE format overflow\r\n");
        return;
    }

    if (udp_uplink_send_line(line)) {
        me->cfire_count++;
        OMC_LOG("[OMC AO_ENG] CFIRE #%d : %s (members=%d, has_tank=%d)\r\n",
                me->cfire_count, line, best.member_count, has_tank ? 1 : 0);
    } else {
        OMC_LOG("[OMC AO_ENG] CFIRE drop (uplink failure): %s\r\n", line);
    }

    MissileInventoryStats s = missile_inventory_stats();
    OMC_LOG("    >> remaining inventory: Cirit=%d MAM-L=%d SOM=%d (total=%d)\r\n",
            s.cirit_available, s.maml_available, s.som_available,
            s.total_available);
}

/* ------------------------------------------------------------------ */
/* HSM state functions                                                */
/* ------------------------------------------------------------------ */

QState eng_initial(Engagement * const me, QEvt const * const e)
{
    (void)e;
    me->fire_count  = 0;
    me->skip_count  = 0;
    me->cfire_count = 0;
    me->cskip_count = 0;
    return Q_TRAN(&eng_disabled);
}

QState eng_disabled(Engagement * const me, QEvt const * const e)
{
    (void)me;
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_ENG] -> Disabled\r\n");
            status = Q_HANDLED();
            break;

        case ENABLE_ENGAGEMENT_SIG:
            status = Q_TRAN(&eng_active);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState eng_active(Engagement * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_ENG] -> Active (monitoring)\r\n");
            status = Q_HANDLED();
            break;

        case TARGET_VERIFIED_SIG: {
            TargetVerifiedEvent const * tev =
                reinterpret_cast<TargetVerifiedEvent const *>(e);
            process_target(me, tev);
            status = Q_HANDLED();
            break;
        }

        case CLUSTER_FIRE_DECISION_SIG: {
            ClusterFireDecisionEvent const * dev =
                reinterpret_cast<ClusterFireDecisionEvent const *>(e);
            process_cluster_decision(me, dev);
            status = Q_HANDLED();
            break;
        }

        case DISABLE_ENGAGEMENT_SIG:
            status = Q_TRAN(&eng_disabled);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ao_engagement_start()
{
    QActive_ctor(&g_eng.super, Q_STATE_CAST(&eng_initial));

    QACTIVE_START(&g_eng.super,
                  ENG_QP_PRIO,
                  g_eng_queue_storage, ENG_QUEUE_LEN,
                  g_eng_stack, ENG_STACK_SIZE,
                  static_cast<void *>(0));

    /* AO comes up Disabled. AO_MissionController posts
     * ENABLE_ENGAGEMENT_SIG when the mission state enters Armed and
     * DISABLE_ENGAGEMENT_SIG on disarm/reset. */
}

QActive* ao_engagement_get()
{
    return &g_eng.super;
}

int ao_engagement_fire_count()
{
    return g_eng.fire_count;
}

int ao_engagement_skip_count()
{
    return g_eng.skip_count;
}

int ao_engagement_cfire_count()
{
    return g_eng.cfire_count;
}

int ao_engagement_cskip_count()
{
    return g_eng.cskip_count;
}

} /* namespace omc */
