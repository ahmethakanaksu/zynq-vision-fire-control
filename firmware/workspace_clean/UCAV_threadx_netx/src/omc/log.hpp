/*
 * log.hpp -- thread-safe UART logging wrapper for the OMC subsystem.
 *
 * xil_printf is not thread-safe. With several ThreadX threads logging
 * concurrently (HSM tick, AO_FrameProcessor, AO_LsrManager, udp_router,
 * AO_Engagement, AO_Telemetry) the per-character output of one thread
 * could interleave with another mid-line, leaving the UART trace
 * unreadable.
 *
 * The fix is a single TX_MUTEX (priority inheritance) guarding every
 * xil_printf call. The OMC_LOG macro is the per-line entry point: lock,
 * print, unlock. For multi-line groups that must stay contiguous, the
 * caller can wrap the block in log_lock()/log_unlock() manually -- the
 * usual rule of "no blocking calls while holding the lock" applies.
 *
 * Before log_init() succeeds the lock/unlock helpers are no-ops, so
 * very-early-boot code can use OMC_LOG without crashing. The legacy C
 * baseline (app_hsm.c, udp_echo.c, ...) keeps using plain xil_printf;
 * its prints are sparse and rarely contend with OMC traffic.
 */

#ifndef OMC_LOG_HPP_
#define OMC_LOG_HPP_

extern "C" {
#include "xil_printf.h"
}

namespace omc {

/* Create the UART mutex. Returns true on success; safe to call once at
 * the very start of project_start(). Idempotent. */
bool log_init();

/* Acquire / release the UART mutex. No-ops before log_init() succeeds. */
void log_lock();
void log_unlock();

} /* namespace omc */

/* Atomic one-line print: lock, xil_printf, unlock. Use this everywhere a
 * thread used to call xil_printf directly. */
#define OMC_LOG(...) do {        \
    omc::log_lock();             \
    xil_printf(__VA_ARGS__);     \
    omc::log_unlock();           \
} while (0)

#endif /* OMC_LOG_HPP_ */
