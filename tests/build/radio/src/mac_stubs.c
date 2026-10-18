/* SPDX-License-Identifier: Apache-2.0 */

/* Stand-ins for functions the stack does not define yet */

#include <zephyr/net/ieee802154_radio.h>

#include "radio/port.h"

void zzb_radio_tx_done(struct zzb_radio_port *port, struct net_buf *buf, enum zzb_mac_status status,
		       bool frame_pending)
{
	net_buf_unref(buf);
}

void zzb_radio_rx(struct zzb_radio_port *port, struct net_buf *buf)
{
	net_buf_unref(buf);
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	return NET_CONTINUE;
}
