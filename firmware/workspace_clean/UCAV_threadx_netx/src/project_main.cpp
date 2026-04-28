/*
 * project_main.cpp -- OMC (Onboard Mission Computer) bring-up.
 *
 * Called once from demo_netx_duo_ping.c after the baseline subsystems
 * are up (UDP echo on 5001, TCP echo on 7, legacy HSM command channel
 * on 5002, AO_Controller demo HSM). This is where the mission-computer
 * subsystem initializes its shared services, starts every Active Object,
 * and brings up the UDP receiver.
 *
 * Order of bring-up is intentional:
 *   1. Initialize the shared services (mutexes, static tables) before
 *      any thread that touches them is created.
 *   2. Bring up the outbound UDP channel; any AO that emits wire
 *      messages depends on it.
 *   3. Initialize the QF event pools, in non-decreasing event-size
 *      order (a hard QP/C requirement).
 *   4. Start the Active Objects -- QP creates one ThreadX thread per AO.
 *   5. Start the udp_router thread; it can post events to AOs as soon
 *      as the first datagram arrives, so all AOs must already be running.
 *   6. Print a one-line readiness summary so the UART trace shows the
 *      final inventory and track-table state.
 */

extern "C" {
#include "xil_printf.h"
}

#include "track_table.hpp"
#include "missile_inventory.hpp"
#include "ao_frame_processor.hpp"
#include "omc/log.hpp"
#include "omc/udp_uplink.hpp"
#include "omc/ao_lsr_manager.hpp"
#include "omc/ao_engagement.hpp"
#include "omc/ao_mission_controller.hpp"
#include "omc/ao_telemetry.hpp"
#include "omc/cluster_table.hpp"
#include "signals.hpp"

extern "C" {
#include "qpc.h"
}

extern "C" void udp_router_start(void);

extern "C" void project_start(void)
{
    /* UART log mutex first: every subsequent OMC_LOG call serializes on
     * it, so the boot trace is interleave-free. Failure is non-fatal --
     * OMC_LOG falls back to bare xil_printf -- so we do not abort here. */
    (void)omc::log_init();

    OMC_LOG("[OMC] subsystem start\r\n");

    /* --- 1. Shared services --- */
    if (!omc::track_table_init()) {
        OMC_LOG("[OMC] FATAL: track_table_init failed; aborting OMC bring-up\r\n");
        return;
    }
    if (!omc::missile_inventory_init()) {
        OMC_LOG("[OMC] FATAL: missile_inventory_init failed; aborting OMC bring-up\r\n");
        return;
    }
    if (!omc::cluster_table_init()) {
        OMC_LOG("[OMC] FATAL: cluster_table_init failed; aborting OMC bring-up\r\n");
        return;
    }

    /* --- 2. Outbound UDP channel --- */
    if (!omc::udp_uplink_init()) {
        OMC_LOG("[OMC] FATAL: udp_uplink_init failed; aborting OMC bring-up\r\n");
        return;
    }

    /* --- 3. QF event pools (one per event type) ---
     * QP/C requires pools to be initialized in non-decreasing event-size
     * order; QF_poolInit asserts (qf_dyn.c:110) otherwise. The order
     * smallest -> largest is:
     *   LsrResultEvent   (~80 B)  -- inbound LSRRES, also hosts
     *                                ClusterRequestEvent and
     *                                ClusterFireDecisionEvent (12 B each)
     *   LsrRequestEvent  (~112 B) -- outbound LSRCMD batch
     *   FrameEvent       (~176 B) -- inbound FRM, also hosts
     *                                ClusterResultEvent (~144 B)
     */
    omc::ao_lsr_manager_init_result_pool();     /* smallest */
    omc::ao_lsr_manager_init_event_pool();      /* medium   */
    omc::ao_frame_processor_init_event_pool();  /* largest  */

    /* --- 4. Active Objects ---
     * Each AO has its own thread, so QP doesn't care about the start
     * order. We start the workers first (they come up Disabled), then
     * the MissionController, then post a boot ARM_SIG so the MC enters
     * Armed.Normal and broadcasts ENABLE to the workers. Removing the
     * boot ARM gives a production build that requires an explicit ARM
     * over the wire before any worker comes online. */
    omc::ao_telemetry_start();            /* prio 2: Disabled at start */
    omc::ao_frame_processor_start();      /* prio 4: Disabled at start */
    omc::ao_lsr_manager_start();          /* prio 5: Disabled at start */
    omc::ao_engagement_start();           /* prio 6: Disabled at start */
    omc::ao_mission_controller_start();   /* prio 7: Idle at start    */

    /* Boot-time auto-ARM: send the MC straight to Armed.Normal so the
     * pipeline is ready as soon as the simulator connects. */
    static QEvt const evt_boot_arm = QEVT_INITIALIZER(omc::ARM_SIG);
    QACTIVE_POST(omc::ao_mission_controller_get(),
                 &evt_boot_arm,
                 static_cast<void *>(0));

    /* --- 5. Worker threads (UDP router) --- */
    udp_router_start();

    /* --- 6. Final readiness summary --- */
    omc::TrackTableStats        ts = omc::track_table_stats();
    omc::MissileInventoryStats  ms = omc::missile_inventory_stats();
    OMC_LOG("[OMC] init summary: tracks=%d/%d  missiles=%d (Cirit=%d MAM-L=%d SOM=%d)\r\n",
               ts.active, (int)omc::MAX_TRACKS,
               ms.total_available,
               ms.cirit_available, ms.maml_available, ms.som_available);

    OMC_LOG("[OMC] subsystem ready\r\n");
}
