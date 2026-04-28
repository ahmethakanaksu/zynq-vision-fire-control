/*
 * ao_lsr_manager.cpp -- LsrManager Active Object implementation.
 *
 * Owns the laser-range handshake in both directions:
 *   single-target  : LsrRequestEvent  -> LSRCMD line
 *                    LsrResultEvent   -> track_table update + optional
 *                                        TargetVerifiedEvent to Engagement
 *   cluster-mode   : ClusterRequestEvent -> CLSRCMD line
 *                    ClusterResultEvent  -> cluster_table update +
 *                                           ClusterFireDecisionEvent to
 *                                           Engagement
 *
 * Wire formats:
 *   LSRCMD,<frame_id>,<count>,<track_id>,<pitch_mdeg>,<yaw_mdeg>,...
 *   CLSRCMD,<frame_id>,<cluster_count>,<cluster_id>,<member_count>,
 *           <track_id>,<pitch>,<yaw>,...
 *
 * QP priority 5, slightly above AO_FrameProcessor (4), so a pending LSR
 * send is not held back behind a long frame-processing run.
 */

#include "ao_lsr_manager.hpp"
#include "events.hpp"
#include "signals.hpp"
#include "udp_uplink.hpp"
#include "lsr_angle.hpp"
#include "ao_engagement.hpp"
#include "track_table.hpp"
#include "cluster_table.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include "log.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace omc {
namespace {

/* ------------------------------------------------------------------ */
/* AO storage                                                         */
/* ------------------------------------------------------------------ */

constexpr unsigned LSR_QP_PRIO        = 5U;
constexpr unsigned LSR_QUEUE_LEN      = 16U;
constexpr unsigned LSR_STACK_SIZE     = 4096U;
constexpr unsigned LSR_REQ_POOL_SIZE  = 8U;
constexpr unsigned LSR_RES_POOL_SIZE  = 8U;

constexpr int LSRCMD_LINE_BUF_SIZE = 768; /* room for 8 items at ~32 chars each */

struct LsrManager {
    QActive super;
    int     sent_count;            /* LSRCMD messages sent */
    int     drop_count;            /* LSR requests dropped (uplink dest unset etc.) */
    int     result_count;          /* LSRRES messages processed */
    int     discovered_count;      /* tracks that transitioned to discovered */
    int     clsr_sent_count;       /* CLSRCMD messages sent */
    int     clsr_drop_count;       /* CLSRCMD batches dropped */
    int     clsr_result_count;     /* CLSRRES batches processed */
    int     clsr_verified_count;   /* clusters that transitioned to verified */
};

LsrManager g_lsr;

QEvtPtr  g_lsr_queue_storage[LSR_QUEUE_LEN];
uint8_t  g_lsr_stack[LSR_STACK_SIZE] __attribute__((aligned(8)));

alignas(8) uint8_t g_lsr_request_pool[LSR_REQ_POOL_SIZE * sizeof(LsrRequestEvent)];
alignas(8) uint8_t g_lsr_result_pool [LSR_RES_POOL_SIZE * sizeof(LsrResultEvent)];

/* ------------------------------------------------------------------ */
/* HSM state function forward declarations                            */
/* ------------------------------------------------------------------ */

QState lsr_initial (LsrManager * const me, QEvt const * const e);
QState lsr_disabled(LsrManager * const me, QEvt const * const e);
QState lsr_active  (LsrManager * const me, QEvt const * const e);
QState lsr_idle    (LsrManager * const me, QEvt const * const e);

/* ------------------------------------------------------------------ */
/* LSRCMD formatting + sending (single-target)                        */
/* ------------------------------------------------------------------ */

void emit_lsrcmd(LsrManager * const me, LsrRequestEvent const * req)
{
    if (req->item_count <= 0) {
        return; /* nothing to send */
    }

    char line[LSRCMD_LINE_BUF_SIZE];
    int  pos = 0;

    int n = std::snprintf(line + pos, sizeof(line) - pos,
                          "LSRCMD,%d,%d",
                          req->frame_id, req->item_count);
    if (n < 0 || n >= static_cast<int>(sizeof(line) - pos)) {
        OMC_LOG("[OMC AO_LSR] format overflow on header\r\n");
        return;
    }
    pos += n;

    for (int i = 0; i < req->item_count; ++i) {
        const LsrRequestItem& it = req->items[i];

        int pitch_mdeg = 0;
        int yaw_mdeg   = 0;
        compute_lsr_angles_mdeg(it.cx, it.cy, pitch_mdeg, yaw_mdeg);

        n = std::snprintf(line + pos, sizeof(line) - pos,
                          ",%03d,%d,%d",
                          it.track_id, pitch_mdeg, yaw_mdeg);
        if (n < 0 || n >= static_cast<int>(sizeof(line) - pos)) {
            OMC_LOG("[OMC AO_LSR] format overflow at item %d\r\n", i);
            return;
        }
        pos += n;
    }

    if (udp_uplink_send_line(line)) {
        me->sent_count++;
        OMC_LOG("[OMC AO_LSR] sent #%d : %s\r\n", me->sent_count, line);
    } else {
        me->drop_count++;
        OMC_LOG("[OMC AO_LSR] drop #%d (uplink dest unset or NX failure): %s\r\n",
                   me->drop_count, line);
    }
}

/* Emit a single CLSRCMD line covering all pending clusters for this
 * frame. Wire format parallels LSRCMD:
 *
 *   CLSRCMD,<frame_id>,<cluster_count>,
 *           <cluster_id>,<member_count>,<track_id>,<pitch>,<yaw>,...
 *           <cluster_id>,<member_count>,<track_id>,<pitch>,<yaw>,...
 *
 * cluster_table is queried under its own mutex; the cached centroid on
 * each PendingClusterMember lets us compute the angles without touching
 * track_table here. */
void emit_clsrcmd(LsrManager * const me, int frame_id)
{
    PendingCluster snap[MAX_PENDING_CLUSTERS];
    const int n_clu = cluster_table_get_pending_for_frame(
        frame_id, snap, MAX_PENDING_CLUSTERS);
    if (n_clu <= 0) {
        me->clsr_drop_count++;
        OMC_LOG("[OMC AO_LSR] CLSRCMD drop (no clusters for frame %d)\r\n",
                   frame_id);
        return;
    }

    char line[LSRCMD_LINE_BUF_SIZE];
    int  pos = 0;

    int n = std::snprintf(line + pos, sizeof(line) - pos,
                          "CLSRCMD,%d,%d", frame_id, n_clu);
    if (n < 0 || n >= static_cast<int>(sizeof(line) - pos)) {
        OMC_LOG("[OMC AO_LSR] CLSRCMD overflow on header\r\n");
        return;
    }
    pos += n;

    for (int c = 0; c < n_clu; ++c) {
        const PendingCluster& pc = snap[c];

        n = std::snprintf(line + pos, sizeof(line) - pos,
                          ",%d,%d", pc.cluster_id, pc.member_count);
        if (n < 0 || n >= static_cast<int>(sizeof(line) - pos)) {
            OMC_LOG("[OMC AO_LSR] CLSRCMD overflow at cluster %d\r\n", c);
            return;
        }
        pos += n;

        for (int m = 0; m < pc.member_count; ++m) {
            const PendingClusterMember& mb = pc.members[m];

            int pitch_mdeg = 0;
            int yaw_mdeg   = 0;
            compute_lsr_angles_mdeg(mb.cx, mb.cy, pitch_mdeg, yaw_mdeg);

            n = std::snprintf(line + pos, sizeof(line) - pos,
                              ",%03d,%d,%d",
                              mb.track_id, pitch_mdeg, yaw_mdeg);
            if (n < 0 || n >= static_cast<int>(sizeof(line) - pos)) {
                OMC_LOG("[OMC AO_LSR] CLSRCMD overflow at cluster %d member %d\r\n",
                           c, m);
                return;
            }
            pos += n;
        }
    }

    if (udp_uplink_send_line(line)) {
        me->clsr_sent_count++;
        OMC_LOG("[OMC AO_LSR] CLSR sent #%d : %s\r\n",
                   me->clsr_sent_count, line);
    } else {
        me->clsr_drop_count++;
        OMC_LOG("[OMC AO_LSR] CLSR drop #%d (uplink dest unset or NX failure): %s\r\n",
                   me->clsr_drop_count, line);
    }
}

/* Apply one LSRRES event: walk its items, push each (track_id, hit) into
 * track_table, log the per-item outcome, and on a "DISCOVERED (new)"
 * transition forward a TargetVerifiedEvent to AO_Engagement so the kill
 * chain can close. */
void process_lsr_result(LsrManager * const me, LsrResultEvent const * res)
{
    me->result_count++;
    OMC_LOG("[OMC AO_LSR] LSRRES frame=%d items=%d (#processed=%d)\r\n",
               res->frame_id, res->item_count, me->result_count);

    for (int i = 0; i < res->item_count; ++i) {
        const LsrResultItem& it = res->items[i];
        const int outcome = track_table_mark_lsr_result(it.track_id, it.hit);

        const char * verb = "unknown";
        switch (outcome) {
            case 0: verb = "no-such-track";        break;
            case 1: verb = "miss-cleared";         break;
            case 2: verb = "DISCOVERED (new)";     break;
            case 3: verb = "discovered (already)"; break;
            default:                                break;
        }

        if (outcome == 2) {
            me->discovered_count++;
        }

        OMC_LOG("    >> track #%d hit=%d -> %s\r\n",
                   it.track_id, it.hit, verb);

        /* Newly discovered hostile: hand off to AO_Engagement. The
         * class is decoded from the track_id (0xx Tank, 1xx Zpt,
         * 2xx Truck, 3xx Civilian), avoiding a track_table acquire on
         * the fast path. */
        if (outcome == 2) {
            VehicleClass cls = VehicleClass::Unknown;
            if (it.track_id >= 0 && it.track_id < 400) {
                cls = static_cast<VehicleClass>(it.track_id / 100);
            }

            /* Civilian / unknown is never engaged: guard before paying
             * the QF allocation. */
            if (cls == VehicleClass::Tank ||
                cls == VehicleClass::Zpt  ||
                cls == VehicleClass::MilitaryTruck) {

                TargetVerifiedEvent * tev =
                    reinterpret_cast<TargetVerifiedEvent *>(
                        QF_newX_(static_cast<uint_fast16_t>(sizeof(TargetVerifiedEvent)),
                                 0U,
                                 static_cast<enum_t>(TARGET_VERIFIED_SIG)));
                if (tev == nullptr) {
                    OMC_LOG("    >> TargetVerified DROPPED (pool exhausted)\r\n");
                } else {
                    tev->frame_id = res->frame_id;
                    tev->track_id = it.track_id;
                    tev->cls      = static_cast<int>(cls);
                    QACTIVE_POST(ao_engagement_get(),
                                 reinterpret_cast<QEvt *>(tev),
                                 static_cast<void *>(0));
                }
            }
        }
    }
}

/* Apply one CLSRRES batch:
 *   - For each (cluster_id, hit), promote pending -> verified on hit or
 *     just close pending on miss. cluster_table.apply returns the
 *     affected member track_ids so we can mirror the update into
 *     track_table (clear lsr_pending; mark discovered on hit).
 *   - Always post one ClusterFireDecisionEvent at the end so
 *     AO_Engagement re-evaluates the verified set. The signal is
 *     idempotent: if nothing eligible exists the engagement handler
 *     returns silently. */
void process_cluster_result(LsrManager * const me, ClusterResultEvent const * cres)
{
    me->clsr_result_count++;
    OMC_LOG("[OMC AO_LSR] CLSRRES frame=%d items=%d (#processed=%d)\r\n",
               cres->frame_id, cres->item_count, me->clsr_result_count);

    int new_verifications = 0;

    for (int i = 0; i < cres->item_count; ++i) {
        const ClusterResultItem& it = cres->items[i];
        const ClusterApplyResult ar =
            cluster_table_apply_cluster_result(it.cluster_id, it.hit);

        const char * verb = "unknown";
        switch (ar.outcome) {
            case 0: verb = "no-such-cluster";        break;
            case 1: verb = "miss-cleared";           break;
            case 2: verb = "VERIFIED (new)";         break;
            case 3: verb = "verified (already)";     break;
            default:                                  break;
        }
        OMC_LOG("    >> cluster #%d hit=%d -> %s (members=%d)\r\n",
                   it.cluster_id, it.hit, verb, ar.member_count);

        /* Mirror member updates into track_table. Mark each member's
         * lsr_pending = false; on hit also mark discovered = true. */
        for (int k = 0; k < ar.member_count; ++k) {
            (void)track_table_mark_lsr_result(ar.track_ids[k], it.hit);
        }

        if (ar.outcome == 2) {
            me->clsr_verified_count++;
            new_verifications++;
        }
    }

    /* Always trigger a fire decision: AO_Engagement is idempotent and
     * returns silently when no eligible verified cluster exists. This
     * keeps the contract simple -- one CLSRRES batch always emits one
     * decision pulse, regardless of outcome. */
    ClusterFireDecisionEvent * dev = reinterpret_cast<ClusterFireDecisionEvent *>(
        QF_newX_(static_cast<uint_fast16_t>(sizeof(ClusterFireDecisionEvent)),
                 0U,
                 static_cast<enum_t>(CLUSTER_FIRE_DECISION_SIG)));
    if (dev == nullptr) {
        OMC_LOG("    >> ClusterFireDecision DROPPED (pool exhausted)\r\n");
        return;
    }
    dev->frame_id = cres->frame_id;

    QACTIVE_POST(ao_engagement_get(),
                 reinterpret_cast<QEvt *>(dev),
                 static_cast<void *>(0));

    OMC_LOG("    >> posted ClusterFireDecision (new verifications=%d)\r\n",
               new_verifications);
}

/* ------------------------------------------------------------------ */
/* HSM state functions                                                */
/* ------------------------------------------------------------------ */

QState lsr_initial(LsrManager * const me, QEvt const * const e)
{
    (void)e;
    me->sent_count          = 0;
    me->drop_count          = 0;
    me->result_count        = 0;
    me->discovered_count    = 0;
    me->clsr_sent_count     = 0;
    me->clsr_drop_count     = 0;
    me->clsr_result_count   = 0;
    me->clsr_verified_count = 0;
    return Q_TRAN(&lsr_disabled);
}

QState lsr_disabled(LsrManager * const me, QEvt const * const e)
{
    (void)me;
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_LSR] -> Disabled\r\n");
            status = Q_HANDLED();
            break;

        case ENABLE_LSR_MANAGER_SIG:
            status = Q_TRAN(&lsr_active);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState lsr_active(LsrManager * const me, QEvt const * const e)
{
    (void)me;
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_LSR] -> Active\r\n");
            status = Q_HANDLED();
            break;

        case Q_INIT_SIG:
            status = Q_TRAN(&lsr_idle);
            break;

        case Q_EXIT_SIG:
            OMC_LOG("[OMC AO_LSR] <- Active\r\n");
            status = Q_HANDLED();
            break;

        case DISABLE_LSR_MANAGER_SIG:
            status = Q_TRAN(&lsr_disabled);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState lsr_idle(LsrManager * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_LSR]    -> Active.Idle (ready)\r\n");
            status = Q_HANDLED();
            break;

        case LSR_REQUEST_NEEDED_SIG: {
            LsrRequestEvent const * req =
                reinterpret_cast<LsrRequestEvent const *>(e);
            emit_lsrcmd(me, req);
            status = Q_HANDLED();
            break;
        }

        case LSR_RESULT_RECEIVED_SIG: {
            LsrResultEvent const * res =
                reinterpret_cast<LsrResultEvent const *>(e);
            process_lsr_result(me, res);
            status = Q_HANDLED();
            break;
        }

        case CLUSTER_REQUEST_NEEDED_SIG: {
            ClusterRequestEvent const * creq =
                reinterpret_cast<ClusterRequestEvent const *>(e);
            emit_clsrcmd(me, creq->frame_id);
            status = Q_HANDLED();
            break;
        }

        case CLUSTER_RESULT_RECEIVED_SIG: {
            ClusterResultEvent const * cres =
                reinterpret_cast<ClusterResultEvent const *>(e);
            process_cluster_result(me, cres);
            status = Q_HANDLED();
            break;
        }

        default:
            status = Q_SUPER(&lsr_active);
            break;
    }
    return status;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ao_lsr_manager_init_event_pool()
{
    QF_poolInit(g_lsr_request_pool,
                static_cast<uint_fast32_t>(sizeof(g_lsr_request_pool)),
                static_cast<uint_fast16_t>(sizeof(LsrRequestEvent)));
}

void ao_lsr_manager_init_result_pool()
{
    QF_poolInit(g_lsr_result_pool,
                static_cast<uint_fast32_t>(sizeof(g_lsr_result_pool)),
                static_cast<uint_fast16_t>(sizeof(LsrResultEvent)));
}

void ao_lsr_manager_start()
{
    QActive_ctor(&g_lsr.super, Q_STATE_CAST(&lsr_initial));

    QACTIVE_START(&g_lsr.super,
                  LSR_QP_PRIO,
                  g_lsr_queue_storage, LSR_QUEUE_LEN,
                  g_lsr_stack, LSR_STACK_SIZE,
                  static_cast<void *>(0));

    /* AO comes up Disabled. AO_MissionController posts ENABLE_LSR_MANAGER
     * when entering Armed and DISABLE_LSR_MANAGER on disarm/reset. */
}

QActive* ao_lsr_manager_get()
{
    return &g_lsr.super;
}

} /* namespace omc */
