/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_pkt.h>

#include "common/buf.h"
#include "common/queue.h"
#include "radio/port.h"

static void rx_handler(struct k_work *work)
{
	struct zzb_radio_port *port = CONTAINER_OF(work, struct zzb_radio_port, rx_work);
	struct net_buf *buf;

	while ((buf = k_fifo_get(&port->rx_fifo, K_NO_WAIT)) != NULL) {
		zzb_radio_rx(port, buf);
	}
}

int zzb_radio_init(struct zzb_radio_port *port, const struct device *dev)
{
	*port = (struct zzb_radio_port){
		.dev = dev,
	};
	k_fifo_init(&port->rx_fifo);
	k_work_init(&port->rx_work, rx_handler);

	return zzb_radio_attach(port);
}

int zzb_radio_recv(struct zzb_radio_port *port, struct net_pkt *pkt)
{
	size_t len = net_pkt_get_len(pkt);
	struct zzb_frame_meta *meta;
	struct net_buf *buf;

	if (IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
		/* nrf5 puts the FCS or the LQI there, esp32 the RSSI and the LQI */
		len -= MIN(len, 2);
	}

	buf = zzb_buf_alloc(K_NO_WAIT);
	if (buf == NULL) {
		return -ENOBUFS;
	}

	net_buf_add(buf, net_buf_linearize(buf->data, net_buf_tailroom(buf), pkt->buffer, 0, len));
	meta = zzb_frame_meta(buf);
	meta->lqi = net_pkt_ieee802154_lqi(pkt);
	meta->rssi = CLAMP(net_pkt_ieee802154_rssi_dbm(pkt), INT8_MIN, INT8_MAX);
	net_pkt_unref(pkt);

	k_fifo_put(&port->rx_fifo, buf);
	k_work_submit_to_queue(&zzb_workq, &port->rx_work);
	return 0;
}
