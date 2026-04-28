/*
 * ao_frame_processor.hpp -- public API of the FrameProcessor Active Object.
 *
 * AO_FrameProcessor consumes FrameEvents posted from the UDP router and
 * drives the per-frame tracking pipeline against the mutex-protected
 * track_table (begin_frame, match_or_create on each detection, end_frame).
 *
 * HSM:
 *
 *     TOP
 *     +-- Disabled   (initial -- ignores frame events; entered at boot
 *     |               and on DISARM/RESET)
 *     +-- Active
 *         +-- Idle   (default substate; handles FRAME_RECEIVED_SIG)
 *
 * Transitions:
 *   ENABLE_FRAME_PROCESSOR_SIG   : Disabled -> Active.Idle
 *   DISABLE_FRAME_PROCESSOR_SIG  : Active.* -> Disabled
 *   FRAME_RECEIVED_SIG (in Idle) : process the frame, remain in Idle
 *
 * Active Object construction is delegated to QP/C: when the AO starts,
 * QP creates a dedicated ThreadX thread that loops on its event queue
 * and dispatches each event to the HSM. There is no polling anywhere --
 * when the queue is empty, the thread is suspended by the kernel.
 */

#ifndef OMC_AO_FRAME_PROCESSOR_HPP_
#define OMC_AO_FRAME_PROCESSOR_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

/* Initialize the QF event pool used for FrameEvent allocations.
 * Must be called once before any QF_NEW(FrameEvent, ...) and before
 * the AO_FrameProcessor thread starts. Safe to call once at boot. */
void ao_frame_processor_init_event_pool();

/* Construct + start the AO_FrameProcessor active object (creates a
 * ThreadX thread + queue under the hood). */
void ao_frame_processor_start();

/* Returns the AO handle so other modules can post events to it. */
QActive* ao_frame_processor_get();

} /* namespace omc */

#endif /* OMC_AO_FRAME_PROCESSOR_HPP_ */
