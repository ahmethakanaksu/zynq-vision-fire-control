/*
 * udp_uplink.hpp -- board-to-simulator outbound UDP channel.
 *
 * Single transmit socket with an auto-learned destination address. The
 * board does not statically know the peer (Unity simulator or test PC);
 * the destination is captured from the source address of the first
 * inbound packet on UDP 5005 and refreshed on every subsequent inbound
 * packet, so a moved peer is followed automatically.
 *
 * Once a destination is known, any thread may call udp_uplink_send_line.
 * Internal serialization is a TX_MUTEX with priority inheritance, so
 * concurrent calls from different AOs are safe and never invert priority.
 *
 * Design choices:
 *   - Source port is fixed (5006) for predictability; the peer can
 *     listen on a deterministic port without negotiation.
 *   - Packets are allocated from the shared NetX pool with NX_NO_WAIT,
 *     so a tight pool never blocks the caller. Send failures are
 *     reported via the bool return; the caller decides how to react.
 *   - The transport is one ASCII line per datagram, keeping the wire
 *     framing identical to the legacy UART link this channel replaced.
 */

#ifndef OMC_UDP_UPLINK_HPP_
#define OMC_UDP_UPLINK_HPP_

extern "C" {
#include "tx_api.h"
#include "nx_api.h"
}

namespace omc {

/* Fixed source port for the TX socket. The simulator listens on this
 * port to receive board-to-PC traffic. */
constexpr UINT UPLINK_SRC_PORT = 5006U;

/* One-time setup of the TX socket and its mutex. Must be called before
 * any send. Returns true on success. */
bool udp_uplink_init();

/* Update the destination address. Called by udp_router on every inbound
 * packet so the board sends back to the actual peer. Until this is
 * called at least once, udp_uplink_send_line returns false. */
void udp_uplink_set_destination(ULONG ip, UINT port);

/* Send one ASCII line. The function appends "\n" so the protocol stays
 * line-oriented even with multiple messages per UDP datagram (we only
 * pack one per datagram for simplicity, but the framing is consistent
 * with the legacy UART transport).
 *
 * Returns true if the datagram was queued for transmission, false if:
 *   - uplink not initialized
 *   - destination not yet learned
 *   - NetX packet allocation or send failed
 */
bool udp_uplink_send_line(const char* line);

} /* namespace omc */

#endif /* OMC_UDP_UPLINK_HPP_ */
