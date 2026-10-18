/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the MAC callbacks until the MAC exists (milestone 6) */

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
