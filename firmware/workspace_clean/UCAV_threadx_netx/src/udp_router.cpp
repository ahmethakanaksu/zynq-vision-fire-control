/*
 * udp_router.cpp -- OMC UDP receiver thread.
 *
 * One ThreadX thread bound to UDP port 5005, blocked in NetX's UDP
 * receive call until a datagram arrives. Each datagram is:
 *   - source-extracted so udp_uplink learns where to send replies,
 *   - trimmed of trailing whitespace,
 *   - dispatched by message kind:
 *       bare keywords (ARM, DISARM, RESET, MODE_NORMAL, MODE_CLUSTER)
 *           -> static QEvt posted to AO_MissionController
 *       FRM    -> ParsedFrame -> FrameEvent  -> AO_FrameProcessor
 *       LSRRES -> ParsedLsrResult  -> LsrResultEvent     -> AO_LsrManager
 *       CLSRRES -> ParsedClsrResult -> ClusterResultEvent -> AO_LsrManager
 *
 * The router does no track / cluster logic itself; it only routes parsed
 * payloads to the right AO. This keeps the rx thread short and keeps
 * domain logic inside the AOs that own the relevant state.
 *
 * Independent of the baseline UDP echo (5001), TCP echo (7), and legacy
 * HSM command channel (5002): own socket, own thread, own buffer.
 */

extern "C" {
#include "tx_api.h"
#include "nx_api.h"
#include "xil_printf.h"
#include "qpc.h"
}

#include <cstring>
#include <cstdint>

#include "protocol.hpp"
#include "events.hpp"
#include "signals.hpp"
#include "ao_frame_processor.hpp"
#include "omc/log.hpp"
#include "omc/udp_uplink.hpp"
#include "omc/ao_lsr_manager.hpp"
#include "omc/ao_mission_controller.hpp"

#define OMC_RX_PORT          5005U
#define OMC_RX_QUEUE_DEPTH   16U
#define OMC_RX_STACK_SIZE    4096U
#define OMC_RX_BUF_SIZE      1024U
#define OMC_RX_THREAD_PRIO   6U

/* NetX IP instance + packet pool created in demo_netx_duo_ping.c. */
extern "C" {
    extern NX_IP            ip_0;
    extern NX_PACKET_POOL   pool_0;
}

static TX_THREAD     g_omc_rx_thread;
static UCHAR         g_omc_rx_stack[OMC_RX_STACK_SIZE] __attribute__((aligned(8)));
static NX_UDP_SOCKET g_omc_rx_socket;

/* Receive buffers; only one thread accesses them. */
static UCHAR         g_omc_rx_buf[OMC_RX_BUF_SIZE];
static char          g_omc_rx_raw[OMC_RX_BUF_SIZE];

/* Print the parsed FRM header and each detection so the UART trace has
 * a record of exactly what the AO is about to receive. */
static void log_parsed_frame(const omc::ParsedFrame& f)
{
    OMC_LOG("[OMC FRM %d] %d detection(s):\r\n",
               f.frame_id, f.detection_count);

    for (int i = 0; i < f.detection_count; ++i) {
        const omc::Detection& d = f.detections[i];
        OMC_LOG("    #%d  cls=%d  cx=%d  cy=%d  w=%d  h=%d\r\n",
                   i, static_cast<int>(d.cls), d.cx, d.cy, d.w, d.h);
    }
}

/* Allocate a LsrResultEvent from the QF pool, copy the parsed result into
 * it, and post to AO_LsrManager. Drop and log if the pool is exhausted. */
static void post_lsrres_to_ao(const omc::ParsedLsrResult& r)
{
    omc::LsrResultEvent * ev = reinterpret_cast<omc::LsrResultEvent *>(
        QF_newX_(static_cast<uint_fast16_t>(sizeof(omc::LsrResultEvent)),
                 0U,
                 static_cast<enum_t>(omc::LSR_RESULT_RECEIVED_SIG)));

    if (ev == nullptr) {
        OMC_LOG("    >> LSRRES DROPPED (pool exhausted)\r\n");
        return;
    }

    ev->frame_id   = r.frame_id;
    ev->item_count = r.item_count;
    for (int i = 0; i < r.item_count; ++i) {
        ev->items[i].track_id = r.items[i].track_id;
        ev->items[i].hit      = r.items[i].hit;
    }

    QACTIVE_POST(omc::ao_lsr_manager_get(),
                 reinterpret_cast<QEvt *>(ev),
                 static_cast<void *>(0));
}

/* Allocate a ClusterResultEvent, copy the parsed CLSRRES into it, post
 * to AO_LsrManager. The event is 144 B so QF allocates it from the
 * FrameEvent pool (172 B) -- the smallest pool that fits, so no new
 * pool is needed for cluster results. */
static void post_clsrres_to_ao(const omc::ParsedClsrResult& r)
{
    omc::ClusterResultEvent * ev = reinterpret_cast<omc::ClusterResultEvent *>(
        QF_newX_(static_cast<uint_fast16_t>(sizeof(omc::ClusterResultEvent)),
                 0U,
                 static_cast<enum_t>(omc::CLUSTER_RESULT_RECEIVED_SIG)));

    if (ev == nullptr) {
        OMC_LOG("    >> CLSRRES DROPPED (pool exhausted)\r\n");
        return;
    }

    ev->frame_id   = r.frame_id;
    ev->item_count = r.item_count;
    for (int i = 0; i < r.item_count; ++i) {
        ev->items[i].cluster_id = r.items[i].cluster_id;
        ev->items[i].hit        = r.items[i].hit;
    }

    QACTIVE_POST(omc::ao_lsr_manager_get(),
                 reinterpret_cast<QEvt *>(ev),
                 static_cast<void *>(0));
}

/* Allocate a FrameEvent from the QF pool, copy the parsed frame into it,
 * and post to AO_FrameProcessor. Drop and log if the pool is exhausted. */
static void post_frame_to_ao(const omc::ParsedFrame& f)
{
    /* QEvt* -> FrameEvent* needs reinterpret_cast in C++: FrameEvent is
     * not a class-derived type of QEvt; it embeds QEvt as its first
     * member, which makes the pointer reinterpretation well defined under
     * the standard-layout common-initial-sequence rule. static_cast
     * between unrelated pointer types is forbidden.
     *
     * Margin = 0U (deliberately NOT QF_NO_MARGIN): under burst load the
     * pool can briefly run dry, and QF_NO_MARGIN would assert
     * (qf_dyn:630). With margin = 0U the allocator returns NULL and we
     * drop the frame with a UART log line -- fail-soft is the right
     * behaviour for a UDP transport, where dropped datagrams are normal. */
    omc::FrameEvent * fe = reinterpret_cast<omc::FrameEvent *>(
        QF_newX_(static_cast<uint_fast16_t>(sizeof(omc::FrameEvent)),
                 0U,
                 static_cast<enum_t>(omc::FRAME_RECEIVED_SIG)));

    if (fe == nullptr) {
        OMC_LOG("    >> DROPPED frame (event pool exhausted)\r\n");
        return;
    }

    fe->frame_id        = f.frame_id;
    fe->detection_count = f.detection_count;

    /* Copy only the active detection slots; rest stays uninitialized. */
    for (int i = 0; i < f.detection_count; ++i) {
        fe->detections[i] = f.detections[i];
    }

    QACTIVE_POST(omc::ao_frame_processor_get(),
                 reinterpret_cast<QEvt *>(fe),
                 static_cast<void *>(0));
}

/*
 * Thread entry: runs forever, blocks on UDP receive, dispatches each
 * datagram. Declared extern "C" so its function-pointer signature
 * matches what tx_thread_create expects (C linkage).
 */
extern "C" void omc_rx_thread_entry(ULONG arg)
{
    (void)arg;

    UINT status = nx_udp_socket_create(&ip_0, &g_omc_rx_socket, "OMC rx",
                                       NX_IP_NORMAL, NX_FRAGMENT_OKAY,
                                       NX_IP_TIME_TO_LIVE,
                                       OMC_RX_QUEUE_DEPTH);
    if (status != NX_SUCCESS) {
        OMC_LOG("[OMC] socket_create failed: 0x%02X\r\n", status);
        return;
    }

    status = nx_udp_socket_bind(&g_omc_rx_socket, OMC_RX_PORT, TX_WAIT_FOREVER);
    if (status != NX_SUCCESS) {
        OMC_LOG("[OMC] socket_bind failed: 0x%02X\r\n", status);
        return;
    }

    OMC_LOG("[OMC] project UDP server listening on port %u\r\n",
               static_cast<unsigned>(OMC_RX_PORT));

    for (;;) {
        NX_PACKET *pkt = nullptr;
        status = nx_udp_socket_receive(&g_omc_rx_socket, &pkt, TX_WAIT_FOREVER);
        if (status != NX_SUCCESS || pkt == nullptr) {
            continue;
        }

        ULONG len = 0;
        status = nx_packet_data_extract_offset(pkt, 0, g_omc_rx_buf,
                                               OMC_RX_BUF_SIZE - 1U, &len);

        /* Capture peer address BEFORE releasing the packet, so udp_uplink
         * knows where to send LSRCMD/FIRE/STATUS replies. nx_udp_source_extract
         * reads from the packet's UDP header, which we still own here. */
        ULONG src_ip = 0;
        UINT  src_port = 0;
        if (status == NX_SUCCESS) {
            (void)nx_udp_source_extract(pkt, &src_ip, &src_port);
        }

        nx_packet_release(pkt);
        if (status != NX_SUCCESS) {
            continue;
        }

        /* Push to uplink so the next outbound message goes back to this peer. */
        if (src_ip != 0U && src_port != 0U) {
            omc::udp_uplink_set_destination(src_ip, src_port);
        }

        /* Trim trailing CR/LF/space/tab so log lines are clean. */
        while (len > 0U &&
               (g_omc_rx_buf[len - 1U] == '\r' ||
                g_omc_rx_buf[len - 1U] == '\n' ||
                g_omc_rx_buf[len - 1U] == ' '  ||
                g_omc_rx_buf[len - 1U] == '\t')) {
            len--;
        }
        g_omc_rx_buf[len] = '\0';

        if (len == 0U) {
            continue;
        }

        /* Save a pristine copy before any in-place tokenization. */
        std::memcpy(g_omc_rx_raw, g_omc_rx_buf, static_cast<size_t>(len) + 1U);

        /* Mission-control bare-keyword commands first (they don't have
         * commas so we can compare directly). Each one posts a static
         * QEvt to AO_MissionController. */
        const char * raw = g_omc_rx_raw;
        bool dispatched_command = false;
        if      (std::strcmp(raw, "ARM")          == 0) {
            static QEvt const evt = QEVT_INITIALIZER(omc::ARM_SIG);
            QACTIVE_POST(omc::ao_mission_controller_get(), &evt, 0);
            dispatched_command = true;
        }
        else if (std::strcmp(raw, "DISARM")       == 0) {
            static QEvt const evt = QEVT_INITIALIZER(omc::DISARM_SIG);
            QACTIVE_POST(omc::ao_mission_controller_get(), &evt, 0);
            dispatched_command = true;
        }
        else if (std::strcmp(raw, "RESET")        == 0) {
            static QEvt const evt = QEVT_INITIALIZER(omc::MISSION_RESET_SIG);
            QACTIVE_POST(omc::ao_mission_controller_get(), &evt, 0);
            dispatched_command = true;
        }
        else if (std::strcmp(raw, "MODE_NORMAL")  == 0) {
            static QEvt const evt = QEVT_INITIALIZER(omc::MODE_NORMAL_SIG);
            QACTIVE_POST(omc::ao_mission_controller_get(), &evt, 0);
            dispatched_command = true;
        }
        else if (std::strcmp(raw, "MODE_CLUSTER") == 0) {
            static QEvt const evt = QEVT_INITIALIZER(omc::MODE_CLUSTER_SIG);
            QACTIVE_POST(omc::ao_mission_controller_get(), &evt, 0);
            dispatched_command = true;
        }

        if (dispatched_command) {
            OMC_LOG("[OMC CMD] %s\r\n", raw);
            continue;
        }

        /* Try FRM first (most frequent message in normal operation). */
        omc::ParsedFrame frame{};
        omc::ParseResult res = omc::parse_frame(
            reinterpret_cast<char *>(g_omc_rx_buf), frame);

        if (res == omc::ParseResult::Ok) {
            log_parsed_frame(frame);
            post_frame_to_ao(frame);
        }
        else if (res != omc::ParseResult::NotMatchingMessage) {
            /* FRM-shaped but malformed. */
            OMC_LOG("[OMC FRM PARSE ERR=%d] %s\r\n",
                       static_cast<int>(res), g_omc_rx_raw);
        }
        else {
            /* Not FRM: try LSRRES on the same (still-pristine) buffer.
             * parse_frame's prefix check failed without mutating, so
             * g_omc_rx_buf is still intact. */
            omc::ParsedLsrResult lsr_res{};
            omc::ParseResult res2 = omc::parse_lsrres(
                reinterpret_cast<char *>(g_omc_rx_buf), lsr_res);

            if (res2 == omc::ParseResult::Ok) {
                OMC_LOG("[OMC LSRRES %d] %d item(s)\r\n",
                           lsr_res.frame_id, lsr_res.item_count);
                for (int i = 0; i < lsr_res.item_count; ++i) {
                    OMC_LOG("    #%d  track_id=%d  hit=%d\r\n",
                               i, lsr_res.items[i].track_id,
                               lsr_res.items[i].hit);
                }
                post_lsrres_to_ao(lsr_res);
            }
            else if (res2 != omc::ParseResult::NotMatchingMessage) {
                OMC_LOG("[OMC LSRRES PARSE ERR=%d] %s\r\n",
                           static_cast<int>(res2), g_omc_rx_raw);
            }
            else {
                /* Not LSRRES either: try CLSRRES on the same buffer.
                 * parse_lsrres' prefix check failed without mutating, so
                 * g_omc_rx_buf is still pristine. */
                omc::ParsedClsrResult clsr_res{};
                omc::ParseResult res3 = omc::parse_clsrres(
                    reinterpret_cast<char *>(g_omc_rx_buf), clsr_res);

                if (res3 == omc::ParseResult::Ok) {
                    OMC_LOG("[OMC CLSRRES %d] %d cluster(s)\r\n",
                               clsr_res.frame_id, clsr_res.item_count);
                    for (int i = 0; i < clsr_res.item_count; ++i) {
                        OMC_LOG("    #%d  cluster_id=%d  hit=%d\r\n",
                                   i, clsr_res.items[i].cluster_id,
                                   clsr_res.items[i].hit);
                    }
                    post_clsrres_to_ao(clsr_res);
                }
                else if (res3 != omc::ParseResult::NotMatchingMessage) {
                    OMC_LOG("[OMC CLSRRES PARSE ERR=%d] %s\r\n",
                               static_cast<int>(res3), g_omc_rx_raw);
                }
                else {
                    /* Not a known structured message: raw log only. */
                    OMC_LOG("[OMC RX %lu] %s\r\n",
                               static_cast<unsigned long>(len),
                               g_omc_rx_raw);
                }
            }
        }
    }
}

/*
 * Public entry: creates the receiver thread. Called once from
 * project_start() after shared services init and AO startup, so the
 * AOs are already running when the first datagram arrives.
 */
extern "C" void udp_router_start(void)
{
    UINT status = tx_thread_create(&g_omc_rx_thread, "omc_rx",
                                   omc_rx_thread_entry, 0,
                                   g_omc_rx_stack, OMC_RX_STACK_SIZE,
                                   OMC_RX_THREAD_PRIO,
                                   OMC_RX_THREAD_PRIO,
                                   TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        OMC_LOG("[OMC] tx_thread_create failed: 0x%02X\r\n", status);
    }
}
