/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>

#include "radio/port.h"

static enum net_verdict l2_recv(struct net_if *iface, struct net_pkt *pkt)
{
	struct zzb_radio_port *port = *(struct zzb_radio_port **)net_if_l2_data(iface);

	if (port == NULL || zzb_radio_recv(port, pkt) < 0) {
		return NET_DROP;
	}

	return NET_OK;
}

/* zzb transmits with zzb_radio_tx(); no IP traffic uses this interface */
static int l2_send(struct net_if *iface, struct net_pkt *pkt)
{
	return -ENOTSUP;
}

NET_L2_INIT(CUSTOM_IEEE802154_L2, l2_recv, l2_send, NULL, NULL);

/* The port attaches to the interface later, in zzb_radio_attach() */
void ieee802154_init(struct net_if *iface)
{
}

int zzb_radio_attach(struct zzb_radio_port *port)
{
	struct net_if *iface = net_if_lookup_by_dev(port->dev);

	if (iface == NULL) {
		return -ENODEV;
	}

	*(struct zzb_radio_port **)net_if_l2_data(iface) = port;
	return 0;
}
