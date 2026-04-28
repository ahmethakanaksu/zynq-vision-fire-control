/*
 * ao_mission_controller.cpp -- top-level command-and-control AO.
 *
 * Owns the mission state machine and gates the worker AOs. On Armed
 * entry it broadcasts ENABLE_* to FrameProcessor, LsrManager, Engagement,
 * and Telemetry; on Armed exit it broadcasts the matching DISABLE_*.
 * Substate transitions inside Armed (Normal <-> Cluster) deliberately
 * stay inside the parent so the workers are not toggled on a mode change.
 *
 * MISSION_RESET clears all three shared tables (tracks, missiles,
 * clusters) atomically so the operator has a single command to bring
 * the platform back to a pristine state without rebooting.
 *
 * QP priority is 7, the highest in the project. All inbound signals are
 * bare (payload-free), so this AO does not need its own event pool.
 */

#include "ao_mission_controller.hpp"
#include "signals.hpp"
#include "log.hpp"
#include "ao_frame_processor.hpp"
#include "ao_lsr_manager.hpp"
#include "ao_engagement.hpp"
#include "ao_telemetry.hpp"
#include "track_table.hpp"
#include "missile_inventory.hpp"
#include "cluster_table.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include <cstdint>

namespace omc {
namespace {

/* ------------------------------------------------------------------ */
/* AO storage                                                         */
/* ------------------------------------------------------------------ */

constexpr unsigned MC_QP_PRIO    = 7U;
constexpr unsigned MC_QUEUE_LEN  = 16U;
constexpr unsigned MC_STACK_SIZE = 4096U;

struct MissionController {
    QActive super;
    int     arm_count;       /* total ARM transitions executed */
    int     disarm_count;
    int     mode_change_count;
    int     reset_count;
};

MissionController g_mc;

QEvtPtr  g_mc_queue_storage[MC_QUEUE_LEN];
uint8_t  g_mc_stack[MC_STACK_SIZE] __attribute__((aligned(8)));

/* Telemetry-readable flags. Written only by the MC thread (on HSM
 * transitions) and read by AO_Telemetry without locking. Single-int
 * reads are atomic on Cortex-A9, so the slight staleness telemetry
 * tolerates is acceptable. */
volatile int g_armed_flag = 0;   /* 0 = Idle, 1 = Armed */
volatile int g_mode_flag  = 0;   /* 0 = Normal, 1 = Cluster */

/* Reusable bare events for broadcast: static, immutable, safe to share
 * across multiple QACTIVE_POST calls. */
QEvt const evt_enable_fp   = QEVT_INITIALIZER(ENABLE_FRAME_PROCESSOR_SIG);
QEvt const evt_disable_fp  = QEVT_INITIALIZER(DISABLE_FRAME_PROCESSOR_SIG);
QEvt const evt_enable_lsr  = QEVT_INITIALIZER(ENABLE_LSR_MANAGER_SIG);
QEvt const evt_disable_lsr = QEVT_INITIALIZER(DISABLE_LSR_MANAGER_SIG);
QEvt const evt_enable_eng  = QEVT_INITIALIZER(ENABLE_ENGAGEMENT_SIG);
QEvt const evt_disable_eng = QEVT_INITIALIZER(DISABLE_ENGAGEMENT_SIG);
QEvt const evt_enable_tel  = QEVT_INITIALIZER(ENABLE_TELEMETRY_SIG);
QEvt const evt_disable_tel = QEVT_INITIALIZER(DISABLE_TELEMETRY_SIG);

/* ------------------------------------------------------------------ */
/* Broadcast helpers                                                  */
/* ------------------------------------------------------------------ */

void broadcast_enable_workers(MissionController * const me)
{
    QACTIVE_POST(ao_frame_processor_get(), &evt_enable_fp,  &me->super);
    QACTIVE_POST(ao_lsr_manager_get(),     &evt_enable_lsr, &me->super);
    QACTIVE_POST(ao_engagement_get(),      &evt_enable_eng, &me->super);
    QACTIVE_POST(ao_telemetry_get(),       &evt_enable_tel, &me->super);
    OMC_LOG("[OMC AO_MC]   broadcast: ENABLE -> FP, LSR, ENG, TEL\r\n");
}

void broadcast_disable_workers(MissionController * const me)
{
    QACTIVE_POST(ao_frame_processor_get(), &evt_disable_fp,  &me->super);
    QACTIVE_POST(ao_lsr_manager_get(),     &evt_disable_lsr, &me->super);
    QACTIVE_POST(ao_engagement_get(),      &evt_disable_eng, &me->super);
    QACTIVE_POST(ao_telemetry_get(),       &evt_disable_tel, &me->super);
    OMC_LOG("[OMC AO_MC]   broadcast: DISABLE -> FP, LSR, ENG, TEL\r\n");
}

void perform_mission_reset(MissionController * const me)
{
    me->reset_count++;
    OMC_LOG("[OMC AO_MC] MISSION_RESET (#%d): clearing tables\r\n",
            me->reset_count);
    track_table_reset();
    missile_inventory_reset();
    cluster_table_reset();
}

/* ------------------------------------------------------------------ */
/* HSM state function forward declarations                            */
/* ------------------------------------------------------------------ */

QState mc_initial(MissionController * const me, QEvt const * const e);
QState mc_idle   (MissionController * const me, QEvt const * const e);
QState mc_armed  (MissionController * const me, QEvt const * const e);
QState mc_normal (MissionController * const me, QEvt const * const e);
QState mc_cluster(MissionController * const me, QEvt const * const e);

/* ------------------------------------------------------------------ */
/* HSM state functions                                                */
/* ------------------------------------------------------------------ */

QState mc_initial(MissionController * const me, QEvt const * const e)
{
    (void)e;
    me->arm_count         = 0;
    me->disarm_count      = 0;
    me->mode_change_count = 0;
    me->reset_count       = 0;
    return Q_TRAN(&mc_idle);
}

QState mc_idle(MissionController * const me, QEvt const * const e)
{
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            g_armed_flag = 0;
            OMC_LOG("[OMC AO_MC] -> Idle (system safe, awaiting ARM)\r\n");
            status = Q_HANDLED();
            break;

        case ARM_SIG:
            me->arm_count++;
            OMC_LOG("[OMC AO_MC] ARM (#%d) accepted\r\n", me->arm_count);
            status = Q_TRAN(&mc_armed);
            break;

        case MISSION_RESET_SIG:
            perform_mission_reset(me);
            status = Q_HANDLED();
            break;

        /* Mode change in Idle is informational; saved for next ARM. */
        case MODE_NORMAL_SIG:
        case MODE_CLUSTER_SIG:
            OMC_LOG("[OMC AO_MC] mode command in Idle ignored (not armed)\r\n");
            status = Q_HANDLED();
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState mc_armed(MissionController * const me, QEvt const * const e)
{
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            g_armed_flag = 1;
            OMC_LOG("[OMC AO_MC] -> Armed\r\n");
            broadcast_enable_workers(me);
            status = Q_HANDLED();
            break;

        case Q_INIT_SIG:
            /* Default substate of Armed = Normal. */
            status = Q_TRAN(&mc_normal);
            break;

        case Q_EXIT_SIG:
            me->disarm_count++;
            OMC_LOG("[OMC AO_MC] <- Armed (disarm #%d)\r\n", me->disarm_count);
            broadcast_disable_workers(me);
            status = Q_HANDLED();
            break;

        case DISARM_SIG:
            status = Q_TRAN(&mc_idle);
            break;

        case MISSION_RESET_SIG:
            /* Reset clears shared tables and drops to Idle. Armed.exit
             * broadcasts DISABLE on the way out; the operator must issue
             * an explicit ARM to come back online. */
            perform_mission_reset(me);
            status = Q_TRAN(&mc_idle);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState mc_normal(MissionController * const me, QEvt const * const e)
{
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            g_mode_flag = 0;
            OMC_LOG("[OMC AO_MC]    -> Armed.Normal\r\n");
            status = Q_HANDLED();
            break;

        case MODE_CLUSTER_SIG:
            me->mode_change_count++;
            OMC_LOG("[OMC AO_MC] MODE_CLUSTER (#%d)\r\n", me->mode_change_count);
            status = Q_TRAN(&mc_cluster);
            break;

        case MODE_NORMAL_SIG:
            /* Already in Normal: idempotent no-op. */
            OMC_LOG("[OMC AO_MC] MODE_NORMAL (already in Normal -- no-op)\r\n");
            status = Q_HANDLED();
            break;

        default:
            status = Q_SUPER(&mc_armed);
            break;
    }
    return status;
}

QState mc_cluster(MissionController * const me, QEvt const * const e)
{
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            g_mode_flag = 1;
            OMC_LOG("[OMC AO_MC]    -> Armed.Cluster\r\n");
            status = Q_HANDLED();
            break;

        case MODE_NORMAL_SIG:
            me->mode_change_count++;
            OMC_LOG("[OMC AO_MC] MODE_NORMAL (#%d)\r\n", me->mode_change_count);
            status = Q_TRAN(&mc_normal);
            break;

        case MODE_CLUSTER_SIG:
            OMC_LOG("[OMC AO_MC] MODE_CLUSTER (already in Cluster -- no-op)\r\n");
            status = Q_HANDLED();
            break;

        default:
            status = Q_SUPER(&mc_armed);
            break;
    }
    return status;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ao_mission_controller_start()
{
    QActive_ctor(&g_mc.super, Q_STATE_CAST(&mc_initial));

    QACTIVE_START(&g_mc.super,
                  MC_QP_PRIO,
                  g_mc_queue_storage, MC_QUEUE_LEN,
                  g_mc_stack, MC_STACK_SIZE,
                  static_cast<void *>(0));
    /* No auto-ARM here. project_main posts ARM_SIG after all AOs are
     * up so the system boots into Armed.Normal for compatibility with
     * existing tests; a future production build can require manual ARM
     * by simply removing that boot post. */
}

QActive* ao_mission_controller_get()
{
    return &g_mc.super;
}

bool ao_mission_controller_is_armed()
{
    return g_armed_flag != 0;
}

int ao_mission_controller_mode()
{
    return g_mode_flag;
}

} /* namespace omc */
