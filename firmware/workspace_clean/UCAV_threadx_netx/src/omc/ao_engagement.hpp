/*
 * ao_engagement.hpp -- public API of the Engagement Active Object.
 *
 * AO_Engagement closes the kill chain. The single-target trigger is
 * AO_LsrManager posting TARGET_VERIFIED_SIG after a "DISCOVERED (new)"
 * transition; the cluster-mode trigger is CLUSTER_FIRE_DECISION_SIG
 * after a CLSRRES batch promotes one or more clusters to verified.
 *
 * On either trigger the AO:
 *   1. Allocates one missile via missile_inventory, applying the
 *      class-aware (single-target) or score-aware (cluster) preference
 *      table.
 *   2. Marks the track (or each cluster member) engaged in the shared
 *      tables.
 *   3. Emits one FIRE / CFIRE wire message via udp_uplink:
 *        FIRE,<frame_id>,<track_id>,<class>,<missile_id>
 *        CFIRE,<decision_frame>,<cluster_id>,<cluster_score>,<missile_id>
 *
 * If no suitable missile is left, the engagement is silently skipped
 * with a UART log line; the cluster stays unengaged and is eligible
 * again on the next decision pulse, so the system degrades gracefully.
 *
 * HSM:
 *
 *     TOP
 *     +-- Disabled  (initial)
 *     +-- Active    (handles TARGET_VERIFIED and CLUSTER_FIRE_DECISION)
 *
 * QP priority is 6, above AO_LsrManager (5) and AO_FrameProcessor (4),
 * so a verified target preempts longer-running frame processing rather
 * than waiting for it to finish.
 */

#ifndef OMC_AO_ENGAGEMENT_HPP_
#define OMC_AO_ENGAGEMENT_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

/* Both TargetVerifiedEvent and ClusterFireDecisionEvent are small
 * (~12 B each) and share the existing LsrResultEvent QF pool, so this
 * AO does not need its own pool. */

void ao_engagement_start();
QActive* ao_engagement_get();

/* Telemetry counters maintained by the AO. Reads of an int are atomic on
 * Cortex-A9 and slightly stale telemetry is acceptable, so the getters
 * intentionally do not lock. */
int ao_engagement_fire_count();
int ao_engagement_skip_count();
int ao_engagement_cfire_count();
int ao_engagement_cskip_count();

} /* namespace omc */

#endif /* OMC_AO_ENGAGEMENT_HPP_ */
