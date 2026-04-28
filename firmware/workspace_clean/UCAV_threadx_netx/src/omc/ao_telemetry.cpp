/*
 * ao_telemetry.cpp -- Telemetry Active Object implementation.
 *
 * Periodic STATUS publisher driven by a per-AO QTimeEvt. The timer is
 * armed on Active.entry (1 second initial, 1 second periodic) and
 * disarmed on Active.exit. Every fire posts TELEMETRY_TICK_SIG to this
 * AO's own queue; the Active state handles it by snapshotting the
 * system and sending one STATUS line.
 *
 * Snapshot reads are best-effort: counters in the other AOs are read
 * without locks (single-int reads are atomic on Cortex-A9). One tick of
 * stale data is acceptable for telemetry and keeps the system
 * responsive under load.
 *
 * QP priority is 2 (lowest in the project) so telemetry can never
 * preempt the frame, LSR or fire-control paths.
 */

#include "ao_telemetry.hpp"
#include "signals.hpp"
#include "log.hpp"
#include "udp_uplink.hpp"
#include "ao_engagement.hpp"
#include "ao_mission_controller.hpp"
#include "track_table.hpp"
#include "missile_inventory.hpp"

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

constexpr unsigned TEL_QP_PRIO    = 2U;
constexpr unsigned TEL_QUEUE_LEN  = 8U;
constexpr unsigned TEL_STACK_SIZE = 4096U;

constexpr unsigned TEL_PERIOD_TICKS = 100U;  /* 100 QF ticks = 1 second */

constexpr int STATUS_LINE_BUF_SIZE = 256;

struct Telemetry {
    QActive  super;
    QTimeEvt tick;
    int      report_count;
};

Telemetry g_tel;

QEvtPtr  g_tel_queue_storage[TEL_QUEUE_LEN];
uint8_t  g_tel_stack[TEL_STACK_SIZE] __attribute__((aligned(8)));

/* ------------------------------------------------------------------ */
/* HSM state function forward declarations                            */
/* ------------------------------------------------------------------ */

QState tel_initial (Telemetry * const me, QEvt const * const e);
QState tel_disabled(Telemetry * const me, QEvt const * const e);
QState tel_active  (Telemetry * const me, QEvt const * const e);

/* ------------------------------------------------------------------ */
/* STATUS publisher                                                   */
/* ------------------------------------------------------------------ */

void publish_status(Telemetry * const me)
{
    me->report_count++;

    const TrackTableStats        ts = track_table_stats();
    const MissileInventoryStats  ms = missile_inventory_stats();
    const bool armed = ao_mission_controller_is_armed();
    const int  mode  = ao_mission_controller_mode();
    const int  fired   = ao_engagement_fire_count();
    const int  skipped = ao_engagement_skip_count();

    char line[STATUS_LINE_BUF_SIZE];
    const int n = std::snprintf(
        line, sizeof(line),
        "STATUS,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
        armed ? 1 : 0,
        mode,
        ts.active,
        ts.hostile,
        ts.discovered,
        ts.lsr_pending,
        ms.cirit_available,
        ms.maml_available,
        ms.som_available,
        fired,
        skipped);
    if (n < 0 || n >= static_cast<int>(sizeof(line))) {
        return;
    }

    const bool tx_ok = udp_uplink_send_line(line);

    /* Throttle UART log so 1 Hz status doesn't drown out other traces.
     * Print the very first one (proof of life) and every 10th after. */
    if (me->report_count == 1 || (me->report_count % 10) == 0) {
        OMC_LOG("[OMC AO_TEL] #%d %s : %s\r\n",
                me->report_count, tx_ok ? "TX" : "drop", line);
    }
}

/* ------------------------------------------------------------------ */
/* HSM state functions                                                */
/* ------------------------------------------------------------------ */

QState tel_initial(Telemetry * const me, QEvt const * const e)
{
    (void)e;
    me->report_count = 0;
    return Q_TRAN(&tel_disabled);
}

QState tel_disabled(Telemetry * const me, QEvt const * const e)
{
    (void)me;
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_TEL] -> Disabled\r\n");
            status = Q_HANDLED();
            break;

        case ENABLE_TELEMETRY_SIG:
            status = Q_TRAN(&tel_active);
            break;

        default:
            status = Q_SUPER(&QHsm_top);
            break;
    }
    return status;
}

QState tel_active(Telemetry * const me, QEvt const * const e)
{
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG:
            OMC_LOG("[OMC AO_TEL] -> Active (1 Hz STATUS)\r\n");
            QTimeEvt_armX(&me->tick,
                          TEL_PERIOD_TICKS,
                          TEL_PERIOD_TICKS); /* periodic */
            status = Q_HANDLED();
            break;

        case Q_EXIT_SIG:
            QTimeEvt_disarm(&me->tick);
            OMC_LOG("[OMC AO_TEL] <- Active (timer disarmed)\r\n");
            status = Q_HANDLED();
            break;

        case TELEMETRY_TICK_SIG:
            publish_status(me);
            status = Q_HANDLED();
            break;

        case DISABLE_TELEMETRY_SIG:
            status = Q_TRAN(&tel_disabled);
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

void ao_telemetry_start()
{
    /* Construct the QTimeEvt first; armX requires it to be initialized. */
    QTimeEvt_ctorX(&g_tel.tick, &g_tel.super,
                   static_cast<enum_t>(TELEMETRY_TICK_SIG), 0U);

    QActive_ctor(&g_tel.super, Q_STATE_CAST(&tel_initial));

    QACTIVE_START(&g_tel.super,
                  TEL_QP_PRIO,
                  g_tel_queue_storage, TEL_QUEUE_LEN,
                  g_tel_stack, TEL_STACK_SIZE,
                  static_cast<void *>(0));
    /* No auto-enable. AO_MissionController will send ENABLE_TELEMETRY
     * on Armed.entry and DISABLE_TELEMETRY on Armed.exit. */
}

QActive* ao_telemetry_get()
{
    return &g_tel.super;
}

} /* namespace omc */
