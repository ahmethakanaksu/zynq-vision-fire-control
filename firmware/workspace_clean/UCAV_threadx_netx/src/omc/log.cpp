/*
 * log.cpp -- thread-safe UART logging wrapper.
 *
 * Single TX_MUTEX with priority inheritance, so a high-priority thread
 * waiting to log never gets blocked behind a low-priority thread that
 * is mid-print.
 */

#include "log.hpp"

extern "C" {
#include "tx_api.h"
}

namespace omc {
namespace {

CHAR     g_mutex_name[] = "omc_log";
TX_MUTEX g_mutex;
bool     g_initialized = false;

} /* anonymous namespace */

bool log_init()
{
    if (g_initialized) {
        return true;
    }
    UINT s = tx_mutex_create(&g_mutex, g_mutex_name, TX_INHERIT);
    if (s != TX_SUCCESS) {
        /* Mutex creation failed: log_lock/unlock degrade to no-ops, so
         * OMC_LOG falls back to bare xil_printf and the system stays
         * usable (just without UART line atomicity). */
        return false;
    }
    g_initialized = true;
    return true;
}

void log_lock()
{
    if (g_initialized) {
        tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    }
}

void log_unlock()
{
    if (g_initialized) {
        tx_mutex_put(&g_mutex);
    }
}

} /* namespace omc */
