/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

#include "common/buf.h"
#include "common/node.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

/* Network addresses from 0xfff8 are broadcast or reserved */
#define FIRST_SPECIAL_ADDR 0xfff8

/* NWK header of a command frame with both IEEE addresses */
#define CMD_HDR_MAX_LEN 24

void zzb_nwk_init(struct zzb_node *node)
{
	/* Safe on the zeroed struct of a new node */
	k_work_cancel_delayable(&node->nwk.keepalive);
	k_work_cancel_delayable(&node->nwk.timer);
	k_work_cancel_delayable(&node->nwk.poll_timer);
	node->nwk = (struct zzb_nwk){0};
	node->nwk.nib = (struct zzb_nib){
		.pan_id = 0xffff,
		.short_addr = 0xffff,
		.seq = sys_rand8_get(),
	};
	zzb_fc_init(&node->nwk.fc, node, "fc/nwk");
	zzb_nwk_ed_timeout_init(node);
	zzb_nwk_rejoin_init(node);
	zzb_nwk_poll_init(node);
}

int zzb_nwk_send(struct zzb_node *node, struct net_buf *buf, struct zzb_nwk_hdr *hdr,
		 uint16_t mac_dst, bool ack)
{
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_nib *nib = &node->nwk.nib;
	bool secure = hdr->flags & ZZB_NWK_SECURITY;
	size_t hdr_len;
	int err;

	hdr->src = nib->short_addr;
	hdr->src_ext = node->eui64;
	hdr->seq = nib->seq;
	if (nib->parent_info != 0) {
		hdr->flags |= ZZB_NWK_ED_INITIATOR;
	}

	hdr_len = zzb_nwk_hdr_len(hdr);
	if (net_buf_headroom(buf) <
	    ZZB_MAC_DATA_HDR_LEN + hdr_len + (secure ? ZZB_NWK_AUX_LEN : 0)) {
		return -EMSGSIZE;
	}

	zzb_nwk_hdr_write(hdr, net_buf_push(buf, hdr_len));
	if (secure) {
		err = zzb_nwk_secure(node, buf, hdr_len);
		if (err) {
			return err;
		}
	}

	meta->dst = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = nib->pan_id,
		.short_addr = mac_dst,
	};
	meta->flags = ack ? ZZB_FRAME_ACK_REQ : 0;

	err = zzb_mcps_data_req(node, buf);
	if (err) {
		return err;
	}

	nib->seq++;
	return 0;
}

int zzb_nwk_send_cmd(struct zzb_node *node, struct zzb_nwk_hdr *hdr, const struct zzb_nwk_cmd *cmd,
		     uint16_t mac_dst, bool ack)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	int err;

	if (buf == NULL) {
		return -ENOBUFS;
	}

	net_buf_reserve(buf, ZZB_MAC_DATA_HDR_LEN + CMD_HDR_MAX_LEN + ZZB_NWK_AUX_LEN);
	net_buf_add(buf, zzb_nwk_cmd_write(cmd, net_buf_tail(buf)));
	zzb_frame_meta(buf)->handle = ZZB_NWK_CMD_HANDLE | cmd->id;
	hdr->type = ZZB_NWK_FRAME_CMD;

	err = zzb_nwk_send(node, buf, hdr, mac_dst, ack);
	if (err) {
		net_buf_unref(buf);
	}

	return err;
}

int zzb_nlde_data_req(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr, uint8_t radius,
		      bool secure)
{
	bool broadcast = zzb_nwk_is_broadcast(dst_addr);
	struct zzb_nwk_neighbor *next_hop = NULL;
	struct zzb_nwk_hdr hdr;

	if ((dst_addr >= FIRST_SPECIAL_ADDR && !broadcast) ||
	    zzb_frame_meta(buf)->handle >= ZZB_NWK_CMD_HANDLE) {
		return -EINVAL;
	}

	if (node->nwk.op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	if (!broadcast) {
		next_hop = zzb_nwk_neighbor_find_short(node, dst_addr);
	}

	if (next_hop == NULL) {
		next_hop = zzb_nwk_neighbor_parent(node);
	}

	if (node->nwk.nib.short_addr >= FIRST_SPECIAL_ADDR || next_hop == NULL) {
		return -ENOTCONN;
	}

	hdr = (struct zzb_nwk_hdr){
		.type = ZZB_NWK_FRAME_DATA,
		.flags = (broadcast ? 0 : ZZB_NWK_DISCOVER_ROUTE) | (secure ? ZZB_NWK_SECURITY : 0),
		.dst = dst_addr,
		.radius = radius ? radius : ZZB_NWK_DEFAULT_RADIUS,
	};

	/* A broadcast from an end device is a MAC unicast to the parent without ACK (R22 3.6.5) */
	return zzb_nwk_send(node, buf, &hdr, next_hop->short_addr, !broadcast);
}

void zzb_mcps_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
	zzb_nwk_parent_tx_status(node, status);

	switch (handle) {
	case ZZB_NWK_CMD_HANDLE | ZZB_NWK_CMD_REJOIN_REQ:
		zzb_nwk_rejoin_req_conf(node, status);
		return;
	case ZZB_NWK_CMD_HANDLE | ZZB_NWK_CMD_LEAVE:
		zzb_nwk_leave_conf(node, status);
		return;
	default:
		break;
	}

	if (handle < ZZB_NWK_CMD_HANDLE) {
		zzb_nlde_data_conf(node, handle, status);
	}
}

static bool for_node(const struct zzb_nib *nib, const struct zzb_nwk_hdr *hdr)
{
	if (hdr->src == nib->short_addr) {
		return false;
	}

	/* The destination of a multicast frame is a group, which APS checks (R22 3.6.6) */
	if (hdr->flags & ZZB_NWK_MULTICAST) {
		return hdr->type == ZZB_NWK_FRAME_DATA;
	}

	/* The receiver stays on until milestone 18, so 0xfffd is accepted */
	return hdr->dst == nib->short_addr || hdr->dst == 0xffff || hdr->dst == 0xfffd;
}

static bool from_candidate(const struct zzb_node *node, const struct zzb_frame_meta *meta)
{
	const struct zzb_nwk_neighbor *nb = node->nwk.candidate;

	return nb != NULL && meta->src.mode == ZZB_MAC_ADDR_SHORT &&
	       meta->src.short_addr == nb->short_addr;
}

/*
 * R22 3.6.2.2: APS takes the Transport Key from unsecured data frames, and a trust center rejoin
 * takes an unsecured rejoin response
 */
static bool unsecured_allowed(const struct zzb_nwk *nwk, const struct zzb_nwk_hdr *hdr)
{
	if (hdr->type == ZZB_NWK_FRAME_DATA) {
		return nwk->key_pending;
	}

	return nwk->op == ZZB_NWK_OP_REJOIN && !nwk->join_secure;
}

/* Returns the header length of an unsecured frame for the node, or a negative error */
static int rx_frame(struct zzb_node *node, struct net_buf *buf, struct zzb_nwk_hdr *hdr)
{
	const struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_nwk_neighbor *nb;
	int hdr_len;
	int err;

	hdr_len = zzb_nwk_hdr_parse(buf->data, buf->len, hdr);
	if (hdr_len < 0) {
		return hdr_len;
	}

	/* An end device drops frames with the end device initiator bit (R22 3.6.2.2) */
	if (!for_node(&node->nwk.nib, hdr) || (hdr->flags & ZZB_NWK_ED_INITIATOR)) {
		return -EINVAL;
	}

	/* While a rejoin runs, only the rejoin response of the candidate is processed */
	if (node->nwk.op == ZZB_NWK_OP_REJOIN &&
	    (hdr->type != ZZB_NWK_FRAME_CMD || !from_candidate(node, meta))) {
		return -EBUSY;
	}

	if (hdr->flags & ZZB_NWK_SECURITY) {
		err = zzb_nwk_unsecure(node, buf, hdr_len);
		if (err) {
			return err;
		}
	} else if (!unsecured_allowed(&node->nwk, hdr)) {
		return -EACCES;
	}

	nb = meta->src.mode == ZZB_MAC_ADDR_SHORT
		     ? zzb_nwk_neighbor_find_short(node, meta->src.short_addr)
		     : NULL;
	if (nb != NULL) {
		nb->lqi = meta->lqi;
	}

	return hdr_len;
}

static void rx_cmd(struct zzb_node *node, struct net_buf *buf, const struct zzb_nwk_hdr *hdr)
{
	const struct zzb_mac_addr *src = &zzb_frame_meta(buf)->src;
	uint16_t mac_src = src->mode == ZZB_MAC_ADDR_SHORT ? src->short_addr : 0xffff;
	struct zzb_nwk_cmd cmd;
	int len = zzb_nwk_cmd_parse(buf->data, buf->len, &cmd);

	/* The commands handled here hold no pointers into the frame */
	net_buf_unref(buf);
	if (len < 0 || (node->nwk.op == ZZB_NWK_OP_REJOIN && cmd.id != ZZB_NWK_CMD_REJOIN_RSP)) {
		return;
	}

	switch (cmd.id) {
	case ZZB_NWK_CMD_NWK_STATUS:
		zzb_nwk_status_rx(node, hdr, &cmd);
		break;
	case ZZB_NWK_CMD_LEAVE:
		zzb_nwk_leave_rx(node, hdr, &cmd, mac_src);
		break;
	case ZZB_NWK_CMD_REJOIN_RSP:
		zzb_nwk_rejoin_rsp_rx(node, hdr, &cmd, mac_src);
		break;
	case ZZB_NWK_CMD_ED_TIMEOUT_RSP:
		zzb_nwk_ed_timeout_rsp_rx(node, hdr, &cmd, mac_src);
		break;
	default:
		break;
	}
}

void zzb_mcps_data_ind(struct zzb_node *node, struct net_buf *buf)
{
	struct zzb_nwk_hdr hdr;
	int hdr_len;

	if (zzb_frame_meta(buf)->flags & ZZB_FRAME_PENDING) {
		zzb_nwk_poll_after(node, K_NO_WAIT);
	}

	hdr_len = rx_frame(node, buf, &hdr);

	if (hdr_len < 0 || zzb_nwk_addr_conflict(node, &hdr)) {
		net_buf_unref(buf);
		return;
	}

	net_buf_pull(buf, hdr_len);
	if (hdr.type == ZZB_NWK_FRAME_CMD) {
		rx_cmd(node, buf, &hdr);
		return;
	}

	zzb_nlde_data_ind(node, buf, (hdr.flags & ZZB_NWK_MULTICAST) ? 0xfffd : hdr.dst, hdr.src,
			  hdr.flags & ZZB_NWK_SECURITY);
}

void zzb_mlme_poll_conf(struct zzb_node *node, enum zzb_mac_status status)
{
	zzb_nwk_parent_tx_status(node, status);
}
