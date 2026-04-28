/*
 * ao_frame_processor.cpp -- FrameProcessor Active Object implementation.
 *
 * One Active Object running on its own ThreadX thread. Consumes
 * FrameEvents posted by udp_router and drives the per-frame MOT
 * tracking pipeline (begin_frame / match_or_create / end_frame) against
 * the mutex-protected track_table.
 *
 * In Cluster mode the same per-frame work runs first; afterwards the AO
 * branches to cluster_table_build_pending and posts a ClusterRequestEvent
 * to AO_LsrManager instead of the per-track LSR hand-off. The normal-mode
 * code path below the branch is byte-for-byte unchanged from the
 * single-target build.
 *
 * Event lifetime: FrameEvents are dynamic, drawn from a dedicated QF
 * event pool sized for FE_POOL_SIZE in-flight events. QP frees the event
 * automatically once its reference count drops to zero.
 *
 * QP priority is 4 (above the legacy AO_Controller at 3, below the LSR
 * and Engagement AOs at 5 and 6).
 */

#include "ao_frame_processor.hpp"
#include "events.hpp"
#include "signals.hpp"
#include "track_table.hpp"
#include "omc/ao_lsr_manager.hpp"
#include "omc/ao_mission_controller.hpp"
#include "omc/cluster_table.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include "omc/log.hpp"

#include <cstring>
#include <cstdint>

namespace omc {
namespace {

/* ------------------------------------------------------------------ */
/* AO storage                                                         */
/* ------------------------------------------------------------------ */

constexpr unsigned FP_QP_PRIO       = 4U;
constexpr unsigned FP_QUEUE_LEN     = 16U;
constexpr unsigned FP_STACK_SIZE    = 4096U;
constexpr unsigned FE_POOL_SIZE     = 16U;

/* Active Object class: POD, embeds QActive as its first member. */
struct FrameProcessor {
    QActive super;
    int     processed_frame_count;
};

FrameProcessor g_fp;

/* Event queue + thread stack for this AO */
QEvtPtr  g_fp_queue_storage[FP_QUEUE_LEN];
uint8_t  g_fp_stack[FP_STACK_SIZE] __attribute__((aligned(8)));

/* Event pool storage for FrameEvent (16 in-flight events).
 * Aligned to 8 bytes to satisfy QP and ARM alignment for embedded structs. */
alignas(8) uint8_t g_frame_event_pool[FE_POOL_SIZE * sizeof(FrameEvent)];

/* ------------------------------------------------------------------ */
/* HSM state function forward declarations                            */
/* ------------------------------------------------------------------ */

QState fp_initial (FrameProcessor * const me, QEvt const * const e);
QState fp_disabled(FrameProcessor * const me, QEvt const * const e);
QState fp_active  (FrameProcessor * const me, QEvt const * const e);
QState fp_idle    (FrameProcessor * const me, QEvt const * const e);

/* ------------------------------------------------------------------ */
/* Frame processing -- invoked from inside fp_idle                    */
/* ------------------------------------------------------------------ */

void process_frame(FrameProcessor * const me, FrameEvent const * fe)
{
    me->processed_frame_count++;

    OMC_LOG("[OMC AO_FP] frame %d (%d det) #processed=%d\r\n",
               fe->frame_id, fe->detection_count,
               me->processed_frame_count);

    track_table_begin_frame();

    for (int i = 0; i < fe->detection_count; ++i) {
        const Detection& d = fe->detections[i];
        MatchOrCreateResult r = track_table_match_or_create(d, fe->frame_id);

        if (!r.ok) {
            OMC_LOG("    >> DROPPED detection (track table full)\r\n");
            continue;
        }
        OMC_LOG("    >> %s track #%d  cls=%d\r\n",
                   r.was_new_track ? "NEW" : "UPD",
                   r.track_id,
                   static_cast<int>(d.cls));
    }

    track_table_end_frame();

    TrackTableStats s = track_table_stats();
    OMC_LOG("    >> table: active=%d hostile=%d discovered=%d\r\n",
               s.active, s.hostile, s.discovered);

    /* --- Branch on mission mode ---
     *
     * Default is Normal (mode == 0). Cluster (mode == 1) is a strict
     * superset added on the side: it routes hostile candidates through
     * cluster_table and emits a single CLSRCMD instead of the per-track
     * LSRCMD batch. The normal-mode path below the branch is unchanged. */
    if (ao_mission_controller_mode() == 1) {
        const int n_built = cluster_table_build_pending(fe->frame_id);
        if (n_built <= 0) {
            return;
        }

        ClusterRequestEvent * creq = reinterpret_cast<ClusterRequestEvent *>(
            QF_newX_(static_cast<uint_fast16_t>(sizeof(ClusterRequestEvent)),
                     0U,
                     static_cast<enum_t>(CLUSTER_REQUEST_NEEDED_SIG)));
        if (creq == nullptr) {
            OMC_LOG("    >> CLUSTER request DROPPED (pool exhausted)\r\n");
            return;
        }
        creq->frame_id = fe->frame_id;

        QACTIVE_POST(ao_lsr_manager_get(),
                     reinterpret_cast<QEvt *>(creq),
                     static_cast<void *>(0));

        OMC_LOG("    >> posted CLUSTER request (%d clusters) to AO_LsrManager\r\n",
                   n_built);
        return;
    }

    /* --- Hand off LSR candidates to AO_LsrManager ---
     *
     * Collect the tracks that need laser-range verification this frame,
     * allocate an LsrRequestEvent, copy the candidate list in, post.
     * Both empty-list and pool-exhausted cases just skip this frame; the
     * next frame retries naturally, which is the right thing on a UDP
     * transport where dropped requests are normal. */
    LsrCandidate cands[MAX_DETECTIONS_PER_FRAME];
    const int n_cands = track_table_collect_lsr_candidates(
        cands, MAX_DETECTIONS_PER_FRAME, fe->frame_id);

    if (n_cands <= 0) {
        return;
    }

    LsrRequestEvent * req = reinterpret_cast<LsrRequestEvent *>(
        QF_newX_(static_cast<uint_fast16_t>(sizeof(LsrRequestEvent)),
                 0U,
                 static_cast<enum_t>(LSR_REQUEST_NEEDED_SIG)));
    if (req == nullptr) {
        OMC_LOG("    >> LSR request DROPPED (pool exhausted)\r\n");
        return;
    }

    req->frame_id   = fe->frame_id;
    req->item_count = n_cands;
    for (int i = 0; i < n_cands; ++i) {
        req->items[i].track_id = cands[i].track_id;
        req->items[i].cx       = cands[i].cx;
        req->items[i].cy       = cands[i].cy;
    }

    QACTIVE_POST(ao_lsr_manager_get(),
                 reinterpret_cast<QEvt *>(req),
                 static_cast<void *>(0));

    OMC_LOG("    >> posted LSR request (%d cand) to AO_LsrManager\r\n",
               n_cands);
}

/* ------------------------------------------------------------------ */
/* HSM state functions                                                */
/* ------------------------------------------------------------------ */

QState fp_initial(FrameProcessor * const me, QEvt const * const e)
{
    (void)e;
    me->processed_frame_count = 0;
    return Q_TRAN(&fp_disabled);
}

QState fp_disabled(FrameProcessor * const me, QEvt const * const e)
{
    (void)me;
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_FP] -> Disabled\r\n");
            status = Q_HANDLED();
            break;

        case ENABLE_FRAME_PROCESSOR_SIG:
            status = Q_TRAN(&fp_active);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState fp_active(FrameProcessor * const me, QEvt const * const e)
{
    (void)me;
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_FP] -> Active\r\n");
            status = Q_HANDLED();
            break;

        case Q_INIT_SIG:
            /* Default substate of Active. */
            status = Q_TRAN(&fp_idle);
            break;

        case Q_EXIT_SIG:
            OMC_LOG("[OMC AO_FP] <- Active\r\n");
            status = Q_HANDLED();
            break;

        case DISABLE_FRAME_PROCESSOR_SIG:
            status = Q_TRAN(&fp_disabled);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState fp_idle(FrameProcessor * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_FP]    -> Active.Idle (ready)\r\n");
            status = Q_HANDLED();
            break;

        case FRAME_RECEIVED_SIG: {
            /* Cast to subclass: safe because QEvt is the first member of
             * FrameEvent and we only ever post FrameEvent on this signal. */
            FrameEvent const * fe =
                reinterpret_cast<FrameEvent const *>(e);
            process_frame(me, fe);
            status = Q_HANDLED();
            break;
        }

        default:
            status = Q_SUPER(&fp_active);
            break;
    }
    return status;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ao_frame_processor_init_event_pool()
{
    QF_poolInit(g_frame_event_pool,
                static_cast<uint_fast32_t>(sizeof(g_frame_event_pool)),
                static_cast<uint_fast16_t>(sizeof(FrameEvent)));
}

void ao_frame_processor_start()
{
    /* Construct the AO with the initial state function. */
    QActive_ctor(&g_fp.super, Q_STATE_CAST(&fp_initial));

    /* Start the AO thread + queue. The QP/ThreadX port creates a
     * dedicated ThreadX thread; after this call the HSM is running and
     * has settled in Disabled. */
    QACTIVE_START(&g_fp.super,
                  FP_QP_PRIO,
                  g_fp_queue_storage, FP_QUEUE_LEN,
                  g_fp_stack, FP_STACK_SIZE,
                  static_cast<void *>(0));

    /* AO comes up Disabled. AO_MissionController posts
     * ENABLE_FRAME_PROCESSOR_SIG on entry to Armed and
     * DISABLE_FRAME_PROCESSOR_SIG on disarm/reset. */
}

QActive* ao_frame_processor_get()
{
    return &g_fp.super;
}

} /* namespace omc */
