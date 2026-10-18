/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "common/buf.h"
#include "common/node.h"
#include "common/timer.h"
#include "mac/frame.h"
#include "mac/mac.h"

/* First form of R22 Tables D-1 and D-2: source PAN ID 0xffff, extended source address */
static void send_cmd(struct zzb_node *node, const struct zzb_mac_cmd *cmd)
{
	struct zzb_mac_hdr hdr = {
		.flags = ZZB_FRAME_ACK_REQ,
		.dst = node->mac.coord,
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0xffff, .ext_addr = node->eui64},
	};

	zzb_mac_send_cmd(node, &hdr, cmd);
}

static void end_assoc(struct zzb_node *node, uint16_t short_addr, enum zzb_mac_status status)
{
	k_work_cancel_delayable(&node->mac.timer);
	zzb_mac_end_op(node);
	zzb_mlme_assoc_conf(node, short_addr, status);
}

/* 802.15.4 5.1.3.1 */
static void fail(struct zzb_node *node, enum zzb_mac_status status)
{
	node->mac.pan_id = 0xffff;
	zzb_mac_set_radio_pan_id(node, 0xffff);
	end_assoc(node, 0xffff, status);
}

int zzb_mlme_assoc_req(struct zzb_node *node, uint8_t channel, const struct zzb_mac_addr *coord,
		       uint8_t capability)
{
	struct zzb_mac *mac = &node->mac;
	const struct zzb_mac_cmd cmd = {
		.id = ZZB_MAC_CMD_ASSOC_REQ,
		.capability = capability,
	};
	int err;

	err = zzb_mac_start_op(node, ZZB_MAC_OP_ASSOC);
	if (err) {
		return err;
	}

	mac->coord = *coord;
	mac->channel = channel;
	mac->pan_id = coord->pan_id;
	if (coord->mode == ZZB_MAC_ADDR_SHORT) {
		mac->coord_short_addr = coord->short_addr;
	} else {
		mac->coord_ext_addr = coord->ext_addr;
	}

	zzb_mac_set_radio_channel(node, channel);
	zzb_mac_set_radio_pan_id(node, mac->pan_id);
	send_cmd(node, &cmd);
	return 0;
}

void zzb_mac_assoc_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending)
{
	if (status != ZZB_MAC_STATUS_SUCCESS) {
		fail(node, status);
		return;
	}

	if (node->mac.op == ZZB_MAC_OP_ASSOC) {
		zzb_timer_arm(&node->mac.timer, K_USEC(ZZB_MAC_RESPONSE_WAIT_US));
		return;
	}

	if (!frame_pending) {
		fail(node, ZZB_MAC_STATUS_NO_DATA);
		return;
	}

	node->mac.op = ZZB_MAC_OP_ASSOC_RX;
	zzb_timer_arm(&node->mac.timer, K_USEC(ZZB_MAC_MAX_FRAME_TOTAL_WAIT_US));
}

void zzb_mac_assoc_timeout(struct zzb_node *node)
{
	const struct zzb_mac_cmd cmd = {
		.id = ZZB_MAC_CMD_DATA_REQ,
	};

	if (node->mac.op == ZZB_MAC_OP_ASSOC_RX) {
		fail(node, ZZB_MAC_STATUS_NO_DATA);
		return;
	}

	node->mac.op = ZZB_MAC_OP_ASSOC_POLL;
	send_cmd(node, &cmd);
}

void zzb_mac_assoc_rx(struct zzb_node *node, struct net_buf *buf)
{
	struct zzb_mac *mac = &node->mac;
	uint64_t src = zzb_frame_meta(buf)->src.ext_addr;
	struct zzb_mac_cmd cmd;
	int len = zzb_mac_cmd_parse(buf->data, buf->len, &cmd);

	net_buf_unref(buf);
	if (len < 0 || cmd.id != ZZB_MAC_CMD_ASSOC_RSP) {
		return;
	}

	if (cmd.assoc_rsp.status != ZZB_MAC_STATUS_SUCCESS) {
		fail(node, cmd.assoc_rsp.status);
		return;
	}

	mac->short_addr = cmd.assoc_rsp.short_addr;
	mac->coord_ext_addr = src;
	zzb_mac_set_radio_short_addr(node, mac->short_addr);
	end_assoc(node, mac->short_addr, ZZB_MAC_STATUS_SUCCESS);
}
