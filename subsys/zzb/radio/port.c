/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_pkt.h>

#include "common/buf.h"
#include "common/queue.h"
#include "radio/port.h"

static enum zzb_mac_status transmit(struct zzb_radio_port *port, struct net_buf *buf)
{
	const struct ieee802154_radio_api *api = port->dev->api;
	int err = api->tx(port->dev, IEEE802154_TX_MODE_CSMA_CA, &port->tx_pkt, buf);

	if (err == -ENOMSG) {
		return ZZB_MAC_STATUS_NO_ACK;
	}

	return err ? ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE : ZZB_MAC_STATUS_SUCCESS;
}

static void tx_handler(struct k_work *work)
{
	struct zzb_radio_port *port = CONTAINER_OF(work, struct zzb_radio_port, tx_work);
	struct net_buf *buf = port->tx_buf;
	enum zzb_mac_status status = transmit(port, buf);

	port->tx_buf = NULL;
	zzb_radio_tx_done(port, buf, status, false);
}

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
	const struct ieee802154_radio_api *api = dev->api;
	enum ieee802154_hw_caps caps = api->get_capabilities(dev);

	if (!(caps & IEEE802154_HW_TX_RX_ACK) || !(caps & IEEE802154_HW_CSMA)) {
		return -ENOTSUP;
	}

	*port = (struct zzb_radio_port){
		.dev = dev,
	};
	k_work_init(&port->tx_work, tx_handler);
	k_fifo_init(&port->rx_fifo);
	k_work_init(&port->rx_work, rx_handler);

	return zzb_radio_attach(port);
}

int zzb_radio_tx(struct zzb_radio_port *port, struct net_buf *buf)
{
	if (port->tx_buf != NULL) {
		return -EBUSY;
	}

	port->tx_buf = buf;
	k_work_submit_to_queue(&zzb_workq, &port->tx_work);
	return 0;
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
