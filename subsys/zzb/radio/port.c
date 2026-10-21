/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/pm/policy.h>
#include <zephyr/random/random.h>

#include "common/buf.h"
#include "common/queue.h"
#include "mac/frame.h"
#include "radio/port.h"

/* Defaults of macMinBE, macMaxBE and macMaxCSMABackoffs (802.15.4 Table 52) */
#define MIN_BE            3
#define MAX_BE            5
#define MAX_CSMA_BACKOFFS 4

/* aUnitBackoffPeriod: 20 symbols of 16 us at 2.4 GHz */
#define UNIT_BACKOFF_US 320

/* tx() blocks the stack queue, so one frame is in flight for all ports */
static bool acked;
static bool ack_pending;

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	struct zzb_mac_hdr hdr;

	if (zzb_mac_hdr_parse(pkt->buffer->data, pkt->buffer->len, &hdr) < 0 ||
	    hdr.type != ZZB_MAC_FRAME_ACK) {
		return NET_CONTINUE;
	}

	acked = true;
	ack_pending = hdr.flags & ZZB_FRAME_PENDING;
	return NET_OK;
}

/* Waits a random number of unit backoff periods, 0 to 2^be - 1 */
static void backoff(uint8_t be)
{
	k_busy_wait((sys_rand32_get() & BIT_MASK(be)) * UNIT_BACKOFF_US);
}

/* Unslotted CSMA-CA (802.15.4 5.1.1.4) */
static int csma_ca(const struct device *dev)
{
	const struct ieee802154_radio_api *api = dev->api;
	uint8_t be = MIN_BE;

	for (int nb = 0; nb <= MAX_CSMA_BACKOFFS; nb++) {
		int err;

		backoff(be);
		err = api->cca(dev);
		if (err != -EBUSY) {
			return err;
		}

		be = MIN(be + 1, MAX_BE);
	}

	return -EBUSY;
}

static enum zzb_mac_status transmit(struct zzb_radio_port *port, struct net_buf *buf)
{
	const struct ieee802154_radio_api *api = port->dev->api;
	enum ieee802154_tx_mode mode = IEEE802154_TX_MODE_CSMA_CA;
	int err;

	acked = false;
	ack_pending = false;

	if (port->hw_csma && IS_ENABLED(CONFIG_ZZB_RADIO_TX_BACKOFF)) {
		backoff(MIN_BE);
	}

	if (!port->hw_csma) {
		if (csma_ca(port->dev)) {
			return ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE;
		}

		mode = IEEE802154_TX_MODE_DIRECT;
	}

	err = api->tx(port->dev, mode, &port->tx_pkt, buf);

	/* esp32 returns 0 also when the ACK is missing */
	if (err == -ENOMSG || (err == 0 && ieee802154_is_ar_flag_set(buf) && !acked)) {
		return ZZB_MAC_STATUS_NO_ACK;
	}

	return err ? ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE : ZZB_MAC_STATUS_SUCCESS;
}

static void tx_handler(struct k_work *work)
{
	struct zzb_radio_port *port = CONTAINER_OF(work, struct zzb_radio_port, tx_work);
	struct net_buf *buf = port->tx_buf;
	enum zzb_mac_status status;

	/* The driver waits for the TX done and the ACK in tx(); a sleep state can stop the radio */
	pm_policy_state_all_lock_get();
	status = transmit(port, buf);
	pm_policy_state_all_lock_put();

	port->tx_buf = NULL;
	zzb_radio_tx_done(port, buf, status, ack_pending);
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

	if (!(caps & IEEE802154_HW_TX_RX_ACK)) {
		return -ENOTSUP;
	}

	*port = (struct zzb_radio_port){
		.dev = dev,
		.hw_csma = caps & IEEE802154_HW_CSMA,
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
