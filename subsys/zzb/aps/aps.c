/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

#include "aps/aps.h"
#include "common/node.h"

void zzb_aps_init(struct zzb_node *node)
{
	/* Safe on the zeroed struct of a new node */
	k_work_cancel_delayable(&node->aps.timer);
	node->aps = (struct zzb_aps){
		.aib.tc_addr = ZZB_APS_TC_UNKNOWN,
		.counter = sys_rand8_get(),
	};
	zzb_fc_init(&node->aps.fc, node, "fc/aps");
	zzb_aps_tx_init(node);
	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_APS, false);
}

void zzb_aps_reset(struct zzb_node *node)
{
	node->aps.aib = (struct zzb_aib){
		.tc_addr = ZZB_APS_TC_UNKNOWN,
	};
	memset(node->aps.dups, 0, sizeof(node->aps.dups));
	zzb_aps_delete(node);
	zzb_aps_binding_delete(node);
	zzb_aps_group_delete(node);
}

/* Whether an endpoint of the node is a member of group */
static bool group_member(const struct zzb_node *node, uint16_t group)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		if (entry->in_use && entry->group == group) {
			return true;
		}
	}

	return false;
}

/* Returns the header length of a frame for the node without APS security, or an error */
static int rx_frame(struct zzb_node *node, struct net_buf *buf, struct zzb_aps_rx *rx)
{
	int hdr_len = zzb_aps_hdr_parse(buf->data, buf->len, &rx->hdr);
	bool link_key;
	int err;

	if (hdr_len < 0) {
		return hdr_len;
	}

	link_key = rx->hdr.flags & ZZB_APS_SECURITY;

	/* A group shares no link key */
	if (rx->hdr.delivery == ZZB_APS_DELIVERY_GROUP &&
	    (link_key || !group_member(node, rx->hdr.group))) {
		return -ENOENT;
	}

	/* Without NWK security only a Transport Key with APS security is accepted (section 11) */
	if (!rx->nwk_security && (rx->hdr.type != ZZB_APS_FRAME_CMD || !link_key)) {
		return -EACCES;
	}

	if (link_key) {
		err = zzb_aps_unsecure(node, buf, hdr_len, rx->src_addr, &rx->sec);
		if (err) {
			return err;
		}
	}

	return hdr_len;
}

/* ACKs end transmissions here; data frames and accepted commands go on */
static bool accepted(struct zzb_node *node, struct net_buf *buf, size_t hdr_len,
		     const struct zzb_aps_rx *rx, struct zzb_aps_cmd *cmd)
{
	switch (rx->hdr.type) {
	case ZZB_APS_FRAME_ACK:
		zzb_aps_ack_rx(node, &rx->hdr, rx->src_addr);
		return false;
	case ZZB_APS_FRAME_CMD:
		return zzb_aps_cmd_parse(buf->data + hdr_len, buf->len - hdr_len, cmd) > 0 &&
		       zzb_aps_cmd_accept(node, rx, cmd);
	default:
		return true;
	}
}

static void deliver(struct zzb_node *node, struct net_buf *buf, const struct zzb_aps_rx *rx,
		    size_t hdr_len)
{
	bool group = rx->hdr.delivery == ZZB_APS_DELIVERY_GROUP;
	const struct zzb_apsde_ind ind = {
		.dst_addr = group ? rx->hdr.group : rx->dst_addr,
		.dst_ep = rx->hdr.dst_ep,
		.src_addr = rx->src_addr,
		.src_ep = rx->hdr.src_ep,
		.profile = rx->hdr.profile,
		.cluster = rx->hdr.cluster,
		.security = (rx->hdr.flags & ZZB_APS_SECURITY) ? ZZB_APS_SECURED_LINK_KEY
							       : ZZB_APS_SECURED_NWK_KEY,
		.group = group,
	};

	net_buf_pull(buf, hdr_len);
	zzb_apsde_data_ind(node, buf, &ind);
}

void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use)
{
	struct zzb_aps_rx rx = {
		.dst_addr = dst_addr,
		.src_addr = src_addr,
		.nwk_security = security_use,
	};
	struct net_buf *ack = NULL;
	struct zzb_aps_cmd cmd;
	int hdr_len = rx_frame(node, buf, &rx);

	if (hdr_len < 0 || !accepted(node, buf, hdr_len, &rx, &cmd)) {
		net_buf_unref(buf);
		return;
	}

	/* Built before the delivery, sent after it (section 11) */
	if ((rx.hdr.flags & ZZB_APS_ACK_REQ) && !zzb_nwk_is_broadcast(dst_addr)) {
		ack = zzb_aps_ack_build(node, &rx.hdr, &rx.sec);
	}

	if (zzb_aps_dup_check(node, src_addr, rx.hdr.counter)) {
		net_buf_unref(buf);
	} else if (rx.hdr.type == ZZB_APS_FRAME_CMD) {
		zzb_aps_dup_add(node, src_addr, rx.hdr.counter);
		/* The command holds no pointers into the frame */
		net_buf_unref(buf);
		zzb_aps_cmd_process(node, &rx, &cmd);
	} else {
		zzb_aps_dup_add(node, src_addr, rx.hdr.counter);
		deliver(node, buf, &rx, hdr_len);
	}

	if (ack != NULL) {
		zzb_aps_send_own(node, ack, src_addr);
	}
}
