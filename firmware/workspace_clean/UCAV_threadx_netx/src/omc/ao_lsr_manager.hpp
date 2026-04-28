/*
 * ao_lsr_manager.hpp -- public API of the LsrManager Active Object.
 *
 * AO_LsrManager owns the laser-range-finder handshake with the simulator.
 * Single-target flow:
 *   AO_FrameProcessor selects hostile tracks needing LSR and posts an
 *   LsrRequestEvent. This AO converts pixel centroids to pitch/yaw in
 *   milli-degrees (compute_lsr_angles_mdeg, no libm), formats one LSRCMD
 *   line, and ships it via udp_uplink. The peer's LSRRES comes back as
 *   an LsrResultEvent and the AO updates track_table; on a "DISCOVERED
 *   (new)" transition it forwards a TargetVerifiedEvent to AO_Engagement.
 *
 * Cluster-mode flow:
 *   On CLUSTER_REQUEST_NEEDED_SIG it pulls the pending clusters from
 *   cluster_table and emits one CLSRCMD; on CLUSTER_RESULT_RECEIVED_SIG
 *   it applies the verifications and posts a ClusterFireDecisionEvent
 *   to AO_Engagement.
 *
 * HSM:
 *
 *     TOP
 *     +-- Disabled  (initial)
 *     +-- Active
 *         +-- Idle  (default substate; handles LSR_REQUEST_NEEDED_SIG,
 *                    LSR_RESULT_RECEIVED_SIG, CLUSTER_REQUEST_NEEDED_SIG,
 *                    CLUSTER_RESULT_RECEIVED_SIG)
 */

#ifndef OMC_AO_LSR_MANAGER_HPP_
#define OMC_AO_LSR_MANAGER_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

/* Pool init for outbound LsrRequestEvent (posted by AO_FrameProcessor). */
void ao_lsr_manager_init_event_pool();

/* Pool init for inbound LsrResultEvent (posted by udp_router on LSRRES).
 * MUST be called BEFORE ao_lsr_manager_init_event_pool() because
 * LsrResultEvent is smaller than LsrRequestEvent and QF_poolInit
 * requires non-decreasing event-size order. */
void ao_lsr_manager_init_result_pool();

void ao_lsr_manager_start();
QActive* ao_lsr_manager_get();

} /* namespace omc */

#endif /* OMC_AO_LSR_MANAGER_HPP_ */
