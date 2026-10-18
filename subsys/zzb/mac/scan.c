/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "common/node.h"
#include "common/timer.h"
#include "mac/frame.h"
#include "mac/mac.h"

/* 2.4 GHz channels */
#define CHANNELS GENMASK(26, 11)

#define MAX_SCAN_DURATION 14

int zzb_mlme_scan_req(struct zzb_node *node, uint32_t channels, uint8_t duration)
{
	struct zzb_mac *mac = &node->mac;
	int err;

	if (channels == 0 || (channels & ~CHANNELS) || duration > MAX_SCAN_DURATION) {
		return -EINVAL;
	}

	err = zzb_mac_start_op(node, ZZB_MAC_OP_SCAN);
	if (err) {
		return err;
	}

	mac->scan_channels = channels;
	mac->scan_duration = duration;
	mac->scan_beacon = false;
	/* Lets beacons of every PAN through (802.15.4 5.1.2.1.2) */
	zzb_mac_set_radio_pan_id(node, 0xffff);
	zzb_mac_scan_next(node);
	return 0;
}

static void end_scan(struct zzb_node *node)
{
	struct zzb_mac *mac = &node->mac;
	enum zzb_mac_status status =
		mac->scan_beacon ? ZZB_MAC_STATUS_SUCCESS : ZZB_MAC_STATUS_NO_BEACON;

	zzb_mac_set_radio_pan_id(node, mac->pan_id);
	zzb_mac_set_radio_channel(node, mac->channel);
	zzb_mac_end_op(node);
	zzb_mlme_scan_conf(node, status);
}

void zzb_mac_scan_next(struct zzb_node *node)
{
	struct zzb_mac *mac = &node->mac;
	struct zzb_mac_hdr hdr = {
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0xffff, .short_addr = 0xffff},
	};
	const struct zzb_mac_cmd cmd = {
		.id = ZZB_MAC_CMD_BEACON_REQ,
	};

	if (mac->scan_channels == 0) {
		end_scan(node);
		return;
	}

	mac->scan_channel = find_lsb_set(mac->scan_channels) - 1;
	mac->scan_channels &= ~BIT(mac->scan_channel);
	zzb_mac_set_radio_channel(node, mac->scan_channel);
	zzb_mac_send_cmd(node, &hdr, &cmd);
}

void zzb_mac_scan_cmd_done(struct zzb_node *node, enum zzb_mac_status status)
{
	uint32_t dwell_us = ZZB_MAC_BASE_SUPERFRAME_US * (BIT(node->mac.scan_duration) + 1);

	if (status != ZZB_MAC_STATUS_SUCCESS) {
		zzb_mac_scan_next(node);
		return;
	}

	zzb_timer_arm(&node->mac.timer, K_USEC(dwell_us));
}

void zzb_mac_scan_rx(struct zzb_node *node, struct net_buf *buf)
{
	uint16_t superframe;
	int len = zzb_mac_beacon_parse(buf->data, buf->len, &superframe);

	if (len < 0) {
		net_buf_unref(buf);
		return;
	}

	net_buf_pull(buf, len);
	node->mac.scan_beacon = true;
	zzb_mlme_beacon_notify_ind(node, buf, node->mac.scan_channel, superframe);
}
