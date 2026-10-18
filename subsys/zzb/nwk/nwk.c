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

void zzb_nwk_init(struct zzb_node *node)
{
	node->nwk = (struct zzb_nwk){0};
	node->nwk.nib = (struct zzb_nib){
		.pan_id = 0xffff,
		.short_addr = 0xffff,
		.seq = sys_rand8_get(),
	};
	zzb_nwk_fc_init(node);
}

/* R22 Table 3-69 */
static bool is_broadcast(uint16_t addr)
{
	return addr == 0xffff || addr == 0xfffd || addr == 0xfffc || addr == 0xfffb;
}

int zzb_nlde_data_req(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr, uint8_t radius,
		      bool secure)
{
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_nib *nib = &node->nwk.nib;
	bool broadcast = is_broadcast(dst_addr);
	struct zzb_nwk_neighbor *next_hop = NULL;
	struct zzb_nwk_hdr hdr;
	size_t hdr_len;
	int err;

	if (dst_addr >= FIRST_SPECIAL_ADDR && !broadcast) {
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

	if (nib->short_addr >= FIRST_SPECIAL_ADDR || next_hop == NULL) {
		return -ENOTCONN;
	}

	hdr = (struct zzb_nwk_hdr){
		.type = ZZB_NWK_FRAME_DATA,
		.flags = (broadcast ? 0 : ZZB_NWK_DISCOVER_ROUTE) | (secure ? ZZB_NWK_SECURITY : 0),
		.dst = dst_addr,
		.src = nib->short_addr,
		.radius = radius ? radius : ZZB_NWK_DEFAULT_RADIUS,
		.seq = nib->seq,
	};
	hdr_len = zzb_nwk_hdr_len(&hdr);
	if (net_buf_headroom(buf) <
	    ZZB_MAC_HDR_MAX_LEN + hdr_len + (secure ? ZZB_NWK_AUX_LEN : 0)) {
		return -EMSGSIZE;
	}

	zzb_nwk_hdr_write(&hdr, net_buf_push(buf, hdr_len));
	if (secure) {
		err = zzb_nwk_secure(node, buf, hdr_len);
		if (err) {
			return err;
		}
	}

	meta->dst = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = nib->pan_id,
		.short_addr = next_hop->short_addr,
	};
	meta->flags = broadcast ? 0 : ZZB_FRAME_ACK_REQ;

	err = zzb_mcps_data_req(node, buf);
	if (err) {
		return err;
	}

	nib->seq++;
	return 0;
}

void zzb_mcps_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
	zzb_nlde_data_conf(node, handle, status);
}

static bool for_node(const struct zzb_nib *nib, const struct zzb_nwk_hdr *hdr)
{
	/* The receiver stays on until milestone 14, so 0xfffd is accepted */
	return hdr->src != nib->short_addr &&
	       (hdr->dst == nib->short_addr || hdr->dst == 0xffff || hdr->dst == 0xfffd);
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

	if (hdr->flags & ZZB_NWK_SECURITY) {
		err = zzb_nwk_unsecure(node, buf, hdr_len);
		if (err) {
			return err;
		}
	} else if (hdr->type != ZZB_NWK_FRAME_DATA || !node->nwk.key_pending) {
		/* R22 3.6.2.2: APS takes the Transport Key from unsecured data frames */
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
	struct zzb_nwk_cmd cmd;
	int len = zzb_nwk_cmd_parse(buf->data, buf->len, &cmd);

	/* The commands handled here hold no pointers into the frame */
	net_buf_unref(buf);
	if (len < 0) {
		return;
	}

	switch (cmd.id) {
	case ZZB_NWK_CMD_NWK_STATUS:
		zzb_nwk_status_rx(node, hdr, &cmd);
		break;
	default:
		break;
	}
}

void zzb_mcps_data_ind(struct zzb_node *node, struct net_buf *buf)
{
	struct zzb_nwk_hdr hdr;
	int hdr_len = rx_frame(node, buf, &hdr);

	if (hdr_len < 0) {
		net_buf_unref(buf);
		return;
	}

	net_buf_pull(buf, hdr_len);
	if (hdr.type == ZZB_NWK_FRAME_CMD) {
		rx_cmd(node, buf, &hdr);
		return;
	}

	zzb_nlde_data_ind(node, buf, hdr.dst, hdr.src, hdr.flags & ZZB_NWK_SECURITY);
}

/* The keepalive uses this confirm */

void zzb_mlme_poll_conf(struct zzb_node *node, enum zzb_mac_status status)
{
}
