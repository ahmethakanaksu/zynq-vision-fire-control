/*
 * tcp_echo.c -- TCP echo server on top of NetX Duo + ThreadX.
 *
 * Listens on TCP port 7. One client at a time:
 *   - server socket listens with relisten
 *   - on accept, the echo loop reads packets and writes them straight back
 *   - on disconnect, the socket goes back to listening
 *
 * Independent of udp_echo.c: own thread, own socket, own port, no shared
 * state. Adding or removing this file has no effect on the UDP echo path.
 */

#include "tx_api.h"
#include "nx_api.h"
#include "xil_printf.h"

#define TCP_ECHO_PORT           7U
#define TCP_ECHO_STACK_SIZE     4096U
#define TCP_WINDOW_SIZE         2048U

/* Provided by demo_netx_duo_ping.c */
extern NX_IP            ip_0;

static NX_TCP_SOCKET    s_tcp_socket;
static TX_THREAD        s_tcp_thread;
static UCHAR            s_tcp_thread_stack[TCP_ECHO_STACK_SIZE]
                        __attribute__((aligned(8)));

/* NetX Duo requires a callback for incoming connection requests on a
 * listening server socket. We just need it for nx_tcp_server_socket_listen. */
static void tcp_echo_listen_callback(NX_TCP_SOCKET *socket_ptr, UINT port)
{
    (void)socket_ptr;
    (void)port;
    /* The accept loop handles connection acceptance; nothing to do here. */
}

static void tcp_echo_thread_entry(ULONG arg)
{
    UINT       status;
    NX_PACKET *pkt;

    (void)arg;

    /* Create TCP socket. */
    status = nx_tcp_socket_create(&ip_0, &s_tcp_socket, "TCP echo",
                                  NX_IP_NORMAL,
                                  NX_FRAGMENT_OKAY,
                                  0x80,                     /* TTL */
                                  TCP_WINDOW_SIZE,          /* RX window */
                                  NX_NULL,                  /* No urgent data callback */
                                  NX_NULL);                 /* No disconnect callback */
    if (status != NX_SUCCESS) {
        xil_printf("[TCP] socket_create failed: 0x%02X\r\n", status);
        return;
    }

    /* Begin listening on our port. */
    status = nx_tcp_server_socket_listen(&ip_0, TCP_ECHO_PORT, &s_tcp_socket,
                                         5, tcp_echo_listen_callback);
    if (status != NX_SUCCESS) {
        xil_printf("[TCP] server_socket_listen(%u) failed: 0x%02X\r\n",
                   (unsigned)TCP_ECHO_PORT, status);
        nx_tcp_socket_delete(&s_tcp_socket);
        return;
    }

    xil_printf("[TCP] echo server listening on port %u\r\n",
               (unsigned)TCP_ECHO_PORT);

    /* Outer loop: handle one client at a time, relisten when each leaves. */
    for (;;) {
        /* Wait for a client to connect. */
        status = nx_tcp_server_socket_accept(&s_tcp_socket, NX_WAIT_FOREVER);
        if (status != NX_SUCCESS) {
            /* Reset socket state and try again. */
            nx_tcp_server_socket_unaccept(&s_tcp_socket);
            nx_tcp_server_socket_relisten(&ip_0, TCP_ECHO_PORT, &s_tcp_socket);
            continue;
        }

        /* Inner loop: echo packets until the client disconnects. */
        for (;;) {
            status = nx_tcp_socket_receive(&s_tcp_socket, &pkt, NX_WAIT_FOREVER);
            if (status != NX_SUCCESS) {
                /* Likely a peer disconnect (NX_NOT_CONNECTED, NX_DISCONNECTED). */
                break;
            }

            /* Send the same packet right back. NetX Duo takes ownership on
             * success; on failure we must release it ourselves. */
            status = nx_tcp_socket_send(&s_tcp_socket, pkt, NX_WAIT_FOREVER);
            if (status != NX_SUCCESS) {
                nx_packet_release(pkt);
                break;
            }
        }

        /* Tear down the connection cleanly and prepare for the next client. */
        nx_tcp_socket_disconnect(&s_tcp_socket, NX_WAIT_FOREVER);
        nx_tcp_server_socket_unaccept(&s_tcp_socket);
        nx_tcp_server_socket_relisten(&ip_0, TCP_ECHO_PORT, &s_tcp_socket);
    }
}

void tcp_echo_start(void)
{
    UINT status = tx_thread_create(&s_tcp_thread, "tcp_echo",
                                   tcp_echo_thread_entry, 0,
                                   s_tcp_thread_stack, TCP_ECHO_STACK_SIZE,
                                   8, 8,
                                   TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        xil_printf("[TCP] thread_create failed: 0x%02X\r\n", status);
    }
}
