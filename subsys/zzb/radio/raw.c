/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net/net_core.h>

#include "radio/port.h"

/* Drivers pass a NULL interface in raw mode, so one port serves the radio */
static struct zzb_radio_port *raw_port;

int zzb_radio_attach(struct zzb_radio_port *port)
{
	raw_port = port;
	return 0;
}

int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
	if (raw_port == NULL) {
		return -ENODEV;
	}

	return zzb_radio_recv(raw_port, pkt);
}
