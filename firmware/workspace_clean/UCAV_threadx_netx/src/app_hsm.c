/*
 * app_hsm.c -- baseline Active Object with a 3-state HSM.
 *
 * This is the original demo HSM that proved QP/C runs cleanly on top of
 * ThreadX, alongside the existing UDP/TCP echo servers. It is kept in
 * the build because the OMC subsystem (FrameProcessor, LsrManager,
 * Engagement, MissionController, Telemetry) is layered on top of the
 * same QP/C runtime; the demo HSM still ticks every second so the UART
 * trace shows the framework heartbeat next to the OMC traffic.
 *
 * State diagram:
 *
 *   TOP
 *    +-- Inactive   (initial)
 *    +-- Active
 *         +-- Normal  (default substate of Active)
 *         +-- Alert
 *
 * Events:
 *   TICK_SIG       periodic from a QP time event (every 1 s)
 *   ACTIVATE_SIG   Inactive -> Active.Normal
 *   DEACTIVATE_SIG Active.* -> Inactive
 *   ALERT_SIG      Active.Normal -> Active.Alert
 *   CLEAR_SIG      Active.Alert -> Active.Normal
 *
 * The HSM is driven externally over UDP port 5002 (ASCII commands
 * "MAKE_ACTIVE", "MAKE_ALERT", "CLEAR_ALERT", "MAKE_INACTIVE"); see
 * udp_command.c.
 */

#include "qpc.h"
#include "tx_api.h"
#include "xil_printf.h"
#include "app_hsm.h"

Q_DEFINE_THIS_MODULE("app_hsm")

/* Application signal enum and AO_Controller handle now live in app_hsm.h
 * so external modules (udp_command.c) can post events here. */

/* ------------------------------------------------------------------------- */
/*  Active Object: AO_Controller                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    QActive    super;        /* inherit QActive base class */
    QTimeEvt   timeEvt;      /* periodic tick time-event */
    uint32_t   tick_count;   /* tick counter for self-stimulation */
} Controller;

/* State function declarations. */
static QState Controller_initial   (Controller * const me, void const * const par);
static QState Controller_inactive  (Controller * const me, QEvt const * const e);
static QState Controller_active    (Controller * const me, QEvt const * const e);
static QState Controller_normal    (Controller * const me, QEvt const * const e);
static QState Controller_alert     (Controller * const me, QEvt const * const e);

/* Singleton instance + opaque AO pointer for the rest of the app. */
static Controller l_controller;
QActive * const AO_Controller = &l_controller.super;

/* AO thread + queue resources (allocated by application). */
#define CONTROLLER_QUEUE_LEN  16U
#define CONTROLLER_STACK_SIZE 4096U
static QEvtPtr      l_controller_queueSto[CONTROLLER_QUEUE_LEN];
static uint8_t      l_controller_stack[CONTROLLER_STACK_SIZE]
                     __attribute__((aligned(8)));

/* ThreadX timer that drives QP time events. Fires every 1 ThreadX tick
 * (10 ms) from the ThreadX timer-thread context, which is safe for the
 * QP tick API. */
static TX_TIMER  l_qp_tick_timer;

static void qp_tick_callback(ULONG arg)
{
    (void)arg;
    QTIMEEVT_TICK_X(0U, &l_qp_tick_timer);
}

/* ------------------------------------------------------------------------- */
/*  Constructor + initial pseudo-state                                       */
/* ------------------------------------------------------------------------- */

void Controller_ctor(void)
{
    Controller * const me = &l_controller;

    QActive_ctor(&me->super, Q_STATE_CAST(&Controller_initial));
    QTimeEvt_ctorX(&me->timeEvt, &me->super, TICK_SIG, 0U);
    me->tick_count = 0U;
}

static QState Controller_initial(Controller * const me, void const * const par)
{
    (void)par;

    /* Arm the periodic tick: first event in 1 s, repeat every 1 s.
     * Tick rate 0, timer in QF_TICK_X units. With 100 Hz tick that's 100. */
    QTimeEvt_armX(&me->timeEvt, 100U, 100U);

    xil_printf("[HSM] Controller initial -> Inactive\r\n");
    return Q_TRAN(&Controller_inactive);
}

/* ------------------------------------------------------------------------- */
/*  State: Inactive                                                          */
/* ------------------------------------------------------------------------- */

static QState Controller_inactive(Controller * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
    case Q_ENTRY_SIG:
        xil_printf("[HSM] -> Inactive (ENTRY)\r\n");
        status = Q_HANDLED();
        break;

    case Q_EXIT_SIG:
        xil_printf("[HSM] <- Inactive (EXIT)\r\n");
        status = Q_HANDLED();
        break;

    case TICK_SIG:
        me->tick_count++;
        /* Heartbeat: print the first tick and every 10th thereafter so the
         * UART log stays readable while the OMC kill-chain is running. The
         * HSM still receives every tick; only the printf is throttled. */
        if (me->tick_count == 1U || (me->tick_count % 10U) == 0U) {
            xil_printf("[HSM] tick %u  state=Inactive\r\n", (unsigned)me->tick_count);
        }
        status = Q_HANDLED();
        break;

    case ACTIVATE_SIG:
        status = Q_TRAN(&Controller_active);
        break;

    default:
        status = Q_SUPER(&QHsm_top);
        break;
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/*  State: Active (parent of Normal and Alert)                               */
/* ------------------------------------------------------------------------- */

static QState Controller_active(Controller * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
    case Q_ENTRY_SIG:
        xil_printf("[HSM] -> Active (ENTRY)\r\n");
        status = Q_HANDLED();
        break;

    case Q_EXIT_SIG:
        xil_printf("[HSM] <- Active (EXIT)\r\n");
        status = Q_HANDLED();
        break;

    case Q_INIT_SIG:
        /* Default substate of Active. */
        status = Q_TRAN(&Controller_normal);
        break;

    case DEACTIVATE_SIG:
        status = Q_TRAN(&Controller_inactive);
        break;

    default:
        status = Q_SUPER(&QHsm_top);
        break;
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/*  State: Active.Normal                                                     */
/* ------------------------------------------------------------------------- */

static QState Controller_normal(Controller * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
    case Q_ENTRY_SIG:
        xil_printf("[HSM]    -> Active.Normal (ENTRY)\r\n");
        status = Q_HANDLED();
        break;

    case Q_EXIT_SIG:
        xil_printf("[HSM]    <- Active.Normal (EXIT)\r\n");
        status = Q_HANDLED();
        break;

    case TICK_SIG:
        me->tick_count++;
        if (me->tick_count == 1U || (me->tick_count % 10U) == 0U) {
            xil_printf("[HSM] tick %u  state=Active.Normal\r\n", (unsigned)me->tick_count);
        }
        status = Q_HANDLED();
        break;

    case ALERT_SIG:
        status = Q_TRAN(&Controller_alert);
        break;

    default:
        status = Q_SUPER(&Controller_active);
        break;
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/*  State: Active.Alert                                                      */
/* ------------------------------------------------------------------------- */

static QState Controller_alert(Controller * const me, QEvt const * const e)
{
    QState status;

    switch (e->sig) {
    case Q_ENTRY_SIG:
        xil_printf("[HSM]    -> Active.Alert (ENTRY)\r\n");
        status = Q_HANDLED();
        break;

    case Q_EXIT_SIG:
        xil_printf("[HSM]    <- Active.Alert (EXIT)\r\n");
        status = Q_HANDLED();
        break;

    case TICK_SIG:
        me->tick_count++;
        if (me->tick_count == 1U || (me->tick_count % 10U) == 0U) {
            xil_printf("[HSM] tick %u  state=Active.Alert\r\n", (unsigned)me->tick_count);
        }
        status = Q_HANDLED();
        break;

    case CLEAR_SIG:
        status = Q_TRAN(&Controller_normal);
        break;

    default:
        status = Q_SUPER(&Controller_active);
        break;
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/*  Public entry: call once after tx_kernel_enter, before any AO runs.       */
/* ------------------------------------------------------------------------- */

void qp_app_start(void)
{
    /* Initialize the QP framework. */
    QF_init();

    /* Construct the AO and start it (creates a ThreadX thread + queue). */
    Controller_ctor();
    QACTIVE_START(AO_Controller,
                  3U,                                    /* QP priority (1=lowest) */
                  l_controller_queueSto, CONTROLLER_QUEUE_LEN,
                  l_controller_stack,    CONTROLLER_STACK_SIZE,
                  (void *)0);

    /* Drive QP time events from a ThreadX tick-driven timer. Fires every
     * ThreadX tick (10 ms). 1 = initial delay (1 tick), 1 = reload. */
    UINT err = tx_timer_create(&l_qp_tick_timer, "qp_tick",
                               qp_tick_callback, 0,
                               1U, 1U, TX_AUTO_ACTIVATE);
    if (err != TX_SUCCESS) {
        xil_printf("[HSM] tx_timer_create failed: 0x%02X\r\n", err);
    }

    /* QF_run() returns immediately on the ThreadX port; ThreadX itself
     * drives the AO threads from this point on. */
    (void)QF_run();

    xil_printf("[HSM] QP started, AO_Controller running\r\n");
}

/* ------------------------------------------------------------------------- */
/*  QP startup callback (required hook)                                      */
/* ------------------------------------------------------------------------- */

void QF_onStartup(void)
{
    /* Nothing to configure at QF startup. ThreadX is already running and
     * its tick is what drives QF_TICK_X(); the QP/ThreadX port hooks
     * into the ThreadX tick automatically. The QF_TICK_X(0U) call lives
     * in board_setup.c (TmrIntrHandler). */
}

/* Required QP hook, invoked from QF_stop(). We never stop QF in this
 * application, so the body is intentionally empty. */
void QF_onCleanup(void)
{
}

void Q_onAssert(char const * const module, int_t const id)
{
    xil_printf("[QP-ASSERT] %s:%d\r\n", module, id);
    /* Halt for development. Replace with a reset / error policy for
     * production builds. */
    for (;;) { }
}
