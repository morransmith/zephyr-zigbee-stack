/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "common/buf.h"
#include "common/node.h"
#include "mac/frame.h"
#include "mac/mac.h"
#include "radio/port.h"

/* The first 2.4 GHz channel */
#define INIT_CHANNEL 11

static void set_filter(const struct device *dev, enum ieee802154_filter_type type,
		       struct ieee802154_filter filter)
{
	const struct ieee802154_radio_api *api = dev->api;

	api->filter(dev, true, type, &filter);
}

void zzb_mac_set_radio_pan_id(struct zzb_node *node, uint16_t pan_id)
{
	set_filter(node->port.dev, IEEE802154_FILTER_TYPE_PAN_ID,
		   (struct ieee802154_filter){.pan_id = pan_id});
}

void zzb_mac_set_radio_short_addr(struct zzb_node *node, uint16_t short_addr)
{
	set_filter(node->port.dev, IEEE802154_FILTER_TYPE_SHORT_ADDR,
		   (struct ieee802154_filter){.short_addr = short_addr});
}

void zzb_mlme_set_pan_id(struct zzb_node *node, uint16_t pan_id)
{
	node->mac.pan_id = pan_id;
	zzb_mac_set_radio_pan_id(node, pan_id);
}

void zzb_mlme_set_short_addr(struct zzb_node *node, uint16_t short_addr)
{
	node->mac.short_addr = short_addr;
	zzb_mac_set_radio_short_addr(node, short_addr);
}

int zzb_mlme_set_channel(struct zzb_node *node, uint8_t channel)
{
	if (node->mac.op != ZZB_MAC_OP_NONE) {
		return -EBUSY;
	}

	node->mac.channel = channel;
	return zzb_mac_set_radio_channel(node, channel);
}

uint64_t zzb_mlme_get_coord_ext_addr(const struct zzb_node *node)
{
	return node->mac.coord_ext_addr;
}

int zzb_mac_set_radio_channel(struct zzb_node *node, uint8_t channel)
{
	const struct ieee802154_radio_api *api = node->port.dev->api;
	int err;

	api->set_channel(node->port.dev, channel);
	err = api->start(node->port.dev);
	return err == -EALREADY ? 0 : err;
}

static void timer_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, mac.timer);

	switch (node->mac.op) {
	case ZZB_MAC_OP_SCAN:
		zzb_mac_scan_next(node);
		break;
	case ZZB_MAC_OP_POLL_RX:
		zzb_mac_poll_timeout(node);
		break;
	case ZZB_MAC_OP_ASSOC:
	case ZZB_MAC_OP_ASSOC_RX:
		zzb_mac_assoc_timeout(node);
		break;
	default:
		break;
	}
}

int zzb_mac_init(struct zzb_node *node, const struct device *radio)
{
	uint8_t ext_addr[8];
	int err;

	err = zzb_radio_init(&node->port, radio);
	if (err) {
		return err;
	}

	node->mac = (struct zzb_mac){
		.coord_short_addr = 0xffff,
		.pan_id = 0xffff,
		.short_addr = 0xffff,
		.dsn = sys_rand8_get(),
		.channel = INIT_CHANNEL,
	};
	k_fifo_init(&node->mac.tx_fifo);
	k_work_init_delayable(&node->mac.timer, timer_handler);

	sys_put_le64(node->eui64, ext_addr);
	set_filter(radio, IEEE802154_FILTER_TYPE_IEEE_ADDR,
		   (struct ieee802154_filter){.ieee_addr = ext_addr});
	zzb_mac_set_radio_short_addr(node, node->mac.short_addr);
	zzb_mac_set_radio_pan_id(node, node->mac.pan_id);

	return zzb_mac_set_radio_channel(node, node->mac.channel);
}

int zzb_mac_start_op(struct zzb_node *node, enum zzb_mac_op op)
{
	struct zzb_mac *mac = &node->mac;

	if (mac->op != ZZB_MAC_OP_NONE) {
		return -EBUSY;
	}

	/* Queued frames would go out on the channel of the scan or of the coordinator */
	if ((op == ZZB_MAC_OP_SCAN || op == ZZB_MAC_OP_ASSOC) && !k_fifo_is_empty(&mac->tx_fifo)) {
		return -EBUSY;
	}

	mac->cmd_buf = zzb_buf_alloc(K_NO_WAIT);
	if (mac->cmd_buf == NULL) {
		return -ENOBUFS;
	}

	mac->op = op;
	return 0;
}

void zzb_mac_end_op(struct zzb_node *node)
{
	net_buf_unref(node->mac.cmd_buf);
	node->mac.cmd_buf = NULL;
	node->mac.op = ZZB_MAC_OP_NONE;
}

void zzb_mac_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending)
{
	switch (node->mac.op) {
	case ZZB_MAC_OP_SCAN:
		zzb_mac_scan_cmd_done(node, status);
		break;
	case ZZB_MAC_OP_POLL:
		zzb_mac_poll_cmd_done(node, status, frame_pending);
		break;
	case ZZB_MAC_OP_ASSOC:
	case ZZB_MAC_OP_ASSOC_POLL:
		zzb_mac_assoc_cmd_done(node, status, frame_pending);
		break;
	default:
		break;
	}
}

static bool same_addr(const struct zzb_mac_addr *a, const struct zzb_mac_addr *b)
{
	if (a->mode != b->mode) {
		return false;
	}

	return a->mode == ZZB_MAC_ADDR_EXT ? a->ext_addr == b->ext_addr
					   : a->short_addr == b->short_addr;
}

void zzb_radio_rx(struct zzb_radio_port *port, struct net_buf *buf)
{
	struct zzb_node *node = CONTAINER_OF(port, struct zzb_node, port);
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_mac_hdr hdr;
	int len = zzb_mac_hdr_parse(buf->data, buf->len, &hdr);

	if (len < 0) {
		net_buf_unref(buf);
		return;
	}

	net_buf_pull(buf, len);
	meta->src = hdr.src;
	meta->dst = hdr.dst;
	meta->flags = hdr.flags;

	if (node->mac.op == ZZB_MAC_OP_SCAN && hdr.type == ZZB_MAC_FRAME_BEACON) {
		zzb_mac_scan_rx(node, buf);
		return;
	}

	if (node->mac.op == ZZB_MAC_OP_POLL_RX && hdr.type == ZZB_MAC_FRAME_DATA &&
	    same_addr(&hdr.src, &node->mac.coord)) {
		zzb_mac_poll_rx(node, buf);
		return;
	}

	if (node->mac.op == ZZB_MAC_OP_ASSOC_RX && hdr.type == ZZB_MAC_FRAME_CMD) {
		zzb_mac_assoc_rx(node, buf);
		return;
	}

	/*
	 * A scan drops all other frames (802.15.4 5.1.2.1.2). A zero-length data frame only ends a
	 * poll (802.15.4 5.1.6.3).
	 */
	if (node->mac.op == ZZB_MAC_OP_SCAN || hdr.type != ZZB_MAC_FRAME_DATA || buf->len == 0) {
		net_buf_unref(buf);
		return;
	}

	zzb_mcps_data_ind(node, buf);
}
