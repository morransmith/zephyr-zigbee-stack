/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "common/node.h"
#include "common/timer.h"
#include "mac/frame.h"
#include "mac/mac.h"

static void end_poll(struct zzb_node *node, enum zzb_mac_status status)
{
	k_work_cancel_delayable(&node->mac.timer);
	zzb_mac_end_op(node);
	zzb_mlme_poll_conf(node, status);
}

int zzb_mlme_poll_req(struct zzb_node *node, const struct zzb_mac_addr *coord)
{
	struct zzb_mac_hdr hdr = {
		.flags = ZZB_FRAME_ACK_REQ,
		.dst = *coord,
		.src = zzb_mac_src_addr(node),
	};
	const struct zzb_mac_cmd cmd = {
		.id = ZZB_MAC_CMD_DATA_REQ,
	};
	int err;

	err = zzb_mac_start_op(node, ZZB_MAC_OP_POLL);
	if (err) {
		return err;
	}

	node->mac.coord = *coord;
	zzb_mac_send_cmd(node, &hdr, &cmd);
	return 0;
}

void zzb_mac_poll_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending)
{
	if (status != ZZB_MAC_STATUS_SUCCESS) {
		end_poll(node, status);
		return;
	}

	if (!frame_pending) {
		end_poll(node, ZZB_MAC_STATUS_NO_DATA);
		return;
	}

	node->mac.op = ZZB_MAC_OP_POLL_RX;
	zzb_timer_arm(&node->mac.timer, K_USEC(ZZB_MAC_MAX_FRAME_TOTAL_WAIT_US));
}

void zzb_mac_poll_timeout(struct zzb_node *node)
{
	end_poll(node, ZZB_MAC_STATUS_NO_DATA);
}

void zzb_mac_poll_rx(struct zzb_node *node, struct net_buf *buf)
{
	/* A zero-length frame means that the coordinator has no data (802.15.4 5.1.6.3) */
	if (buf->len == 0) {
		net_buf_unref(buf);
		end_poll(node, ZZB_MAC_STATUS_NO_DATA);
		return;
	}

	end_poll(node, ZZB_MAC_STATUS_SUCCESS);
	zzb_mcps_data_ind(node, buf);
}
