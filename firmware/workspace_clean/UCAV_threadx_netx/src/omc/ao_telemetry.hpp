/*
 * ao_telemetry.hpp -- public API of the Telemetry Active Object.
 *
 * Periodic 1 Hz STATUS publisher. A QP QTimeEvt re-arms every 100 QF
 * ticks (1 s at the 100 Hz tick rate) while the AO is in Active. Each
 * tick snapshots the live mission state under the relevant mutexes and
 * pushes one CSV line to the simulator via udp_uplink.
 *
 * Wire format:
 *   STATUS,<armed>,<mode>,<tracks>,<hostile>,<discovered>,<pending>,
 *          <cirit>,<maml>,<som>,<fired>,<skipped>
 *
 *   armed         0 = Idle, 1 = Armed
 *   mode          0 = Normal, 1 = Cluster (only meaningful when armed)
 *   tracks        active tracks in track_table
 *   hostile       active hostile tracks
 *   discovered    active tracks marked discovered (LSR-confirmed)
 *   pending       active tracks waiting on an LSR result
 *   cirit/maml/som   remaining missiles per type
 *   fired         lifetime FIRE messages emitted by AO_Engagement
 *   skipped       lifetime engagements skipped (no suitable missile)
 *
 * HSM:
 *
 *     TOP
 *     +-- Disabled (initial; no telemetry traffic)
 *     +-- Active   (1 Hz STATUS publish)
 *
 * QP priority is 2 -- the lowest among project AOs -- so telemetry can
 * never preempt mission-critical work.
 */

#ifndef OMC_AO_TELEMETRY_HPP_
#define OMC_AO_TELEMETRY_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

void ao_telemetry_start();
QActive* ao_telemetry_get();

} /* namespace omc */

#endif /* OMC_AO_TELEMETRY_HPP_ */
