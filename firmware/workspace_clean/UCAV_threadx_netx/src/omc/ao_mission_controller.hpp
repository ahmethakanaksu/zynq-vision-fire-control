/*
 * ao_mission_controller.hpp -- public API of the MissionController AO.
 *
 * Top-level command-and-control HSM for the onboard mission computer.
 * It owns the operational mode and gates the worker AOs (FrameProcessor,
 * LsrManager, Engagement, Telemetry) by broadcasting ENABLE/DISABLE
 * signals on parent-state transitions. Substate transitions inside Armed
 * (Normal <-> Cluster) deliberately do not exit the parent, so the
 * worker AOs stay enabled across mode switches.
 *
 * HSM:
 *
 *     TOP
 *     +-- Idle     (initial -- system safe, no fire authorization)
 *     +-- Armed    (operational; workers are enabled)
 *         +-- Normal   (default; single-target engagement)
 *         +-- Cluster  (DBSCAN-style cluster engagement)
 *
 * External commands arrive as plain-ASCII keywords on UDP 5005 and are
 * translated to QP signals by udp_router:
 *   ARM            : Idle -> Armed.Normal
 *   DISARM         : Armed.* -> Idle
 *   MISSION_RESET  : Any -> reset shared tables and go to Idle
 *   MODE_NORMAL    : Armed.Cluster -> Armed.Normal
 *   MODE_CLUSTER   : Armed.Normal -> Armed.Cluster
 *
 * QP priority is 7 (highest among project AOs), so mission-level
 * decisions always preempt worker traffic.
 */

#ifndef OMC_AO_MISSION_CONTROLLER_HPP_
#define OMC_AO_MISSION_CONTROLLER_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

void ao_mission_controller_start();
QActive* ao_mission_controller_get();

/* Telemetry getters: best-effort lock-free reads of the mission state.
 * is_armed() returns true while in any Armed.* substate; mode() returns
 * 0 for Normal and 1 for Cluster, only meaningful while armed. */
bool ao_mission_controller_is_armed();
int  ao_mission_controller_mode();

} /* namespace omc */

#endif /* OMC_AO_MISSION_CONTROLLER_HPP_ */
