/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_pkt.h>

int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
	net_pkt_unref(pkt);
	return 0;
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	return NET_CONTINUE;
}
