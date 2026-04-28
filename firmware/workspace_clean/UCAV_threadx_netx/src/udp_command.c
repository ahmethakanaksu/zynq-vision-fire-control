/*
 * udp_command.c -- UDP command server for the legacy Controller AO.
 *
 * Listens on UDP port 5002. Each datagram is a plain ASCII command
 * (trailing \r / \n stripped). Recognized commands map 1:1 to QP signals
 * posted to AO_Controller:
 *
 *   "ACTIVATE"   -> ACTIVATE_SIG
 *   "DEACTIVATE" -> DEACTIVATE_SIG
 *   "ALERT"      -> ALERT_SIG
 *   "CLEAR"      -> CLEAR_SIG
 *
 * Unknown strings are logged and ignored. There is no reply -- this is
 * a command channel, not an echo server.
 *
 * Independent from udp_echo.c (port 5001) and tcp_echo.c (port 7); has
 * its own NX_UDP_SOCKET and its own ThreadX thread, so the existing
 * echo and ICMP services keep running unchanged.
 */

#include <string.h>
#include "tx_api.h"
#include "nx_api.h"
#include "qpc.h"
#include "xil_printf.h"
#include "app_hsm.h"

#define UDP_CMD_PORT          5002
#define UDP_CMD_QUEUE_DEPTH   8U
#define UDP_CMD_STACK_SIZE    4096U
#define UDP_CMD_BUF_SIZE      64U

/* NetX IP instance + packet pool created in demo_netx_duo_ping.c. */
extern NX_IP            ip_0;
extern NX_PACKET_POOL   pool_0;

static TX_THREAD        l_cmd_thread;
static UCHAR            l_cmd_stack[UDP_CMD_STACK_SIZE] __attribute__((aligned(8)));
static NX_UDP_SOCKET    l_cmd_socket;

/* Static, immutable QP events: safe to post repeatedly. */
static QEvt const evt_activate   = QEVT_INITIALIZER(ACTIVATE_SIG);
static QEvt const evt_deactivate = QEVT_INITIALIZER(DEACTIVATE_SIG);
static QEvt const evt_alert      = QEVT_INITIALIZER(ALERT_SIG);
static QEvt const evt_clear      = QEVT_INITIALIZER(CLEAR_SIG);

static void udp_cmd_thread_entry(ULONG arg)
{
    (void)arg;

    UINT status = nx_udp_socket_create(&ip_0, &l_cmd_socket, "UDP cmd",
                                       NX_IP_NORMAL, NX_FRAGMENT_OKAY,
                                       NX_IP_TIME_TO_LIVE,
                                       UDP_CMD_QUEUE_DEPTH);
    if (status != NX_SUCCESS) {
        xil_printf("[CMD] socket_create failed: 0x%02X\r\n", status);
        return;
    }

    status = nx_udp_socket_bind(&l_cmd_socket, UDP_CMD_PORT, TX_WAIT_FOREVER);
    if (status != NX_SUCCESS) {
        xil_printf("[CMD] socket_bind failed: 0x%02X\r\n", status);
        return;
    }

    xil_printf("[CMD] HSM command server listening on UDP port %d\r\n",
               UDP_CMD_PORT);

    for (;;) {
        NX_PACKET *pkt = NULL;
        status = nx_udp_socket_receive(&l_cmd_socket, &pkt, TX_WAIT_FOREVER);
        if (status != NX_SUCCESS || pkt == NULL) {
            continue;
        }

        UCHAR buf[UDP_CMD_BUF_SIZE];
        ULONG len = 0;
        status = nx_packet_data_extract_offset(pkt, 0, buf,
                                               UDP_CMD_BUF_SIZE - 1U, &len);
        nx_packet_release(pkt);
        if (status != NX_SUCCESS) {
            continue;
        }

        /* Trim trailing CR/LF/space and null-terminate. */
        while (len > 0U &&
               (buf[len - 1U] == '\r' ||
                buf[len - 1U] == '\n' ||
                buf[len - 1U] == ' '  ||
                buf[len - 1U] == '\t')) {
            len--;
        }
        buf[len] = '\0';
        if (len == 0U) {
            continue;
        }

        QEvt const *e = NULL;
        if      (strcmp((char *)buf, "ACTIVATE")   == 0) e = &evt_activate;
        else if (strcmp((char *)buf, "DEACTIVATE") == 0) e = &evt_deactivate;
        else if (strcmp((char *)buf, "ALERT")      == 0) e = &evt_alert;
        else if (strcmp((char *)buf, "CLEAR")      == 0) e = &evt_clear;
        else {
            xil_printf("[CMD] unknown: '%s'\r\n", buf);
            continue;
        }

        xil_printf("[CMD] %s -> AO_Controller\r\n", buf);
        QACTIVE_POST(AO_Controller, e, 0);
    }
}

void udp_command_start(void)
{
    UINT status = tx_thread_create(&l_cmd_thread, "udp_cmd",
                                   udp_cmd_thread_entry, 0,
                                   l_cmd_stack, UDP_CMD_STACK_SIZE,
                                   5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        xil_printf("[CMD] tx_thread_create failed: 0x%02X\r\n", status);
    }
}
