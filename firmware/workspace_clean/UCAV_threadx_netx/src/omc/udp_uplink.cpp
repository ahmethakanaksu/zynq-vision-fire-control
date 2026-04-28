/*
 * udp_uplink.cpp -- board-to-peer TX channel implementation.
 *
 * One NetX UDP socket bound to the fixed source port UPLINK_SRC_PORT.
 * The destination is captured the first time udp_router sees an
 * inbound packet, and refreshed on every subsequent inbound packet, so
 * the board always sends back to the actual peer. Until the destination
 * is known udp_uplink_send_line returns false; callers log a "drop"
 * and continue, which is what lets the 1 Hz STATUS publisher start
 * cleanly before the simulator has connected.
 *
 * Thread safety: a single TX_MUTEX (priority inheritance) serializes
 * destination updates and the send call. NetX itself is thread-safe,
 * but the mutex gives us a consistent (ip, port) snapshot even when
 * udp_router and several AOs touch the channel concurrently.
 */

#include "udp_uplink.hpp"

extern "C" {
#include "xil_printf.h"
}

#include "log.hpp"

#include <cstring>

/* NetX IP instance + packet pool created in demo_netx_duo_ping.c. */
extern "C" {
    extern NX_IP            ip_0;
    extern NX_PACKET_POOL   pool_0;
}

namespace omc {
namespace {

CHAR            g_mutex_name[]   = "omc_tx";
CHAR            g_socket_name[]  = "OMC tx";

TX_MUTEX        g_mutex;
NX_UDP_SOCKET   g_socket;

ULONG           g_dest_ip   = 0U;        /* 0 = not yet learned */
UINT            g_dest_port = 0U;
bool            g_initialized = false;

constexpr UINT  UPLINK_QUEUE_DEPTH = 16U;

} /* anonymous namespace */

bool udp_uplink_init()
{
    if (g_initialized) {
        return true;
    }

    UINT s = tx_mutex_create(&g_mutex, g_mutex_name, TX_INHERIT);
    if (s != TX_SUCCESS) {
        OMC_LOG("[OMC TX] tx_mutex_create failed 0x%02X\r\n", static_cast<unsigned>(s));
        return false;
    }

    s = nx_udp_socket_create(&ip_0, &g_socket, g_socket_name,
                             NX_IP_NORMAL, NX_FRAGMENT_OKAY,
                             NX_IP_TIME_TO_LIVE,
                             UPLINK_QUEUE_DEPTH);
    if (s != NX_SUCCESS) {
        OMC_LOG("[OMC TX] nx_udp_socket_create failed 0x%02X\r\n", static_cast<unsigned>(s));
        return false;
    }

    s = nx_udp_socket_bind(&g_socket, UPLINK_SRC_PORT, TX_WAIT_FOREVER);
    if (s != NX_SUCCESS) {
        OMC_LOG("[OMC TX] nx_udp_socket_bind(%u) failed 0x%02X\r\n",
                   static_cast<unsigned>(UPLINK_SRC_PORT),
                   static_cast<unsigned>(s));
        return false;
    }

    g_initialized = true;
    OMC_LOG("[OMC TX] uplink ready (src port %u, dest learned at first RX)\r\n",
               static_cast<unsigned>(UPLINK_SRC_PORT));
    return true;
}

void udp_uplink_set_destination(ULONG ip, UINT port)
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);

    /* Log only on change to avoid spamming UART when peer is steady. */
    const bool changed = (ip != g_dest_ip) || (port != g_dest_port);
    g_dest_ip   = ip;
    g_dest_port = port;

    tx_mutex_put(&g_mutex);

    if (changed) {
        OMC_LOG("[OMC TX] destination set to %lu.%lu.%lu.%lu:%u\r\n",
                   (ip >> 24) & 0xFFUL,
                   (ip >> 16) & 0xFFUL,
                   (ip >>  8) & 0xFFUL,
                   (ip      ) & 0xFFUL,
                   static_cast<unsigned>(port));
    }
}

bool udp_uplink_send_line(const char* line)
{
    if (!g_initialized || line == nullptr) {
        return false;
    }

    /* Snapshot destination under the mutex. */
    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    const ULONG dest_ip   = g_dest_ip;
    const UINT  dest_port = g_dest_port;
    tx_mutex_put(&g_mutex);

    if (dest_ip == 0U || dest_port == 0U) {
        return false; /* destination not learned yet */
    }

    NX_PACKET *pkt = nullptr;
    UINT s = nx_packet_allocate(&pool_0, &pkt, NX_UDP_PACKET, NX_NO_WAIT);
    if (s != NX_SUCCESS || pkt == nullptr) {
        return false;
    }

    const ULONG line_len = static_cast<ULONG>(std::strlen(line));

    s = nx_packet_data_append(pkt,
                              const_cast<char *>(line),
                              line_len,
                              &pool_0,
                              NX_NO_WAIT);
    if (s == NX_SUCCESS) {
        const char nl = '\n';
        s = nx_packet_data_append(pkt,
                                  const_cast<char *>(&nl),
                                  1U,
                                  &pool_0,
                                  NX_NO_WAIT);
    }
    if (s != NX_SUCCESS) {
        nx_packet_release(pkt);
        return false;
    }

    /* nx_udp_socket_send takes ownership of pkt: it will release the
     * packet on either success or failure inside NetX. We must NOT call
     * nx_packet_release on success. */
    s = nx_udp_socket_send(&g_socket, pkt, dest_ip, dest_port);
    if (s != NX_SUCCESS) {
        /* On failure NetX docs say the caller still owns the packet. */
        nx_packet_release(pkt);
        return false;
    }

    return true;
}

} /* namespace omc */
