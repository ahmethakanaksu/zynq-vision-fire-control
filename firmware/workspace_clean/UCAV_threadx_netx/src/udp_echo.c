/*
 * udp_echo.c -- UDP echo server on top of NetX Duo + ThreadX.
 *
 * Listens on UDP port 5001. Any datagram received is sent back to the
 * sender unchanged. The received NX_PACKET is reused for the response
 * (zero copy) to keep latency as low as possible.
 *
 * Wire-up: call udp_echo_start() from tx_application_define() after
 * nx_udp_enable(&ip_0). A new ThreadX thread is created at priority 8
 * (between the IP thread at 1 and the demo threads at higher priority)
 * which owns the UDP socket for its lifetime.
 */

#include "tx_api.h"
#include "nx_api.h"
#include "xil_printf.h"

#define UDP_ECHO_PORT           5001U
#define UDP_ECHO_STACK_SIZE     4096U

/* Provided by demo_netx_duo_ping.c */
extern NX_IP            ip_0;

static NX_UDP_SOCKET    s_udp_socket;
static TX_THREAD        s_udp_thread;
static UCHAR            s_udp_thread_stack[UDP_ECHO_STACK_SIZE]
                        __attribute__((aligned(8)));

static void udp_echo_thread_entry(ULONG arg)
{
    UINT       status;
    NX_PACKET *pkt;
    ULONG      sender_ip;
    UINT       sender_port;

    (void)arg;

    /* Create the UDP socket. */
    status = nx_udp_socket_create(&ip_0, &s_udp_socket, "UDP echo",
                                  NX_IP_NORMAL,
                                  NX_FRAGMENT_OKAY,
                                  0x80,           /* Time-to-live */
                                  16);            /* Max queue depth */
    if (status != NX_SUCCESS) {
        xil_printf("[UDP] nx_udp_socket_create failed: 0x%02X\r\n", status);
        return;
    }

    /* Bind to our port. */
    status = nx_udp_socket_bind(&s_udp_socket, UDP_ECHO_PORT, NX_WAIT_FOREVER);
    if (status != NX_SUCCESS) {
        xil_printf("[UDP] nx_udp_socket_bind(%u) failed: 0x%02X\r\n",
                   (unsigned)UDP_ECHO_PORT, status);
        nx_udp_socket_delete(&s_udp_socket);
        return;
    }

    xil_printf("[UDP] echo server listening on port %u\r\n",
               (unsigned)UDP_ECHO_PORT);

    /* Echo loop. */
    for (;;) {
        status = nx_udp_socket_receive(&s_udp_socket, &pkt, NX_WAIT_FOREVER);
        if (status != NX_SUCCESS) {
            continue;
        }

        /* Find out who sent it. */
        if (nx_udp_source_extract(pkt, &sender_ip, &sender_port) != NX_SUCCESS) {
            nx_packet_release(pkt);
            continue;
        }

        /* Send the same packet back. nx_udp_socket_send takes ownership;
         * if it fails we must release the packet ourselves. */
        status = nx_udp_socket_send(&s_udp_socket, pkt, sender_ip, sender_port);
        if (status != NX_SUCCESS) {
            nx_packet_release(pkt);
        }
    }
}

void udp_echo_start(void)
{
    UINT status = tx_thread_create(&s_udp_thread, "udp_echo",
                                   udp_echo_thread_entry, 0,
                                   s_udp_thread_stack, UDP_ECHO_STACK_SIZE,
                                   8, 8,
                                   TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        xil_printf("[UDP] tx_thread_create failed: 0x%02X\r\n", status);
    }
}
