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
}

void zzb_aps_reset(struct zzb_node *node)
{
	node->aps.aib = (struct zzb_aib){
		.tc_addr = ZZB_APS_TC_UNKNOWN,
	};
	memset(node->aps.dups, 0, sizeof(node->aps.dups));
	zzb_aps_delete(node);
}

/*
 * Returns the header length of a frame for the node without APS security, or an error. sec is the
 * key that removed the APS security.
 */
static int rx_frame(struct zzb_node *node, struct net_buf *buf, uint16_t src_addr,
		    bool security_use, struct zzb_aps_hdr *hdr, struct zzb_aps_sec *sec)
{
	int hdr_len = zzb_aps_hdr_parse(buf->data, buf->len, hdr);
	bool link_key;
	int err;

	if (hdr_len < 0) {
		return hdr_len;
	}

	link_key = hdr->flags & ZZB_APS_SECURITY;

	/* Without a group table no endpoint is a member of a group */
	if (hdr->delivery == ZZB_APS_DELIVERY_GROUP) {
		return -ENOENT;
	}

	/* Without NWK security only a Transport Key with APS security is accepted (section 11) */
	if (!security_use && (hdr->type != ZZB_APS_FRAME_CMD || !link_key)) {
		return -EACCES;
	}

	if (link_key) {
		err = zzb_aps_unsecure(node, buf, hdr_len, src_addr, sec);
		if (err) {
			return err;
		}
	}

	return hdr_len;
}

static void deliver(struct zzb_node *node, struct net_buf *buf, const struct zzb_aps_hdr *hdr,
		    size_t hdr_len, uint16_t dst_addr, uint16_t src_addr)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = dst_addr,
		.dst_ep = hdr->dst_ep,
		.src_addr = src_addr,
		.src_ep = hdr->src_ep,
		.profile = hdr->profile,
		.cluster = hdr->cluster,
		.security = (hdr->flags & ZZB_APS_SECURITY) ? ZZB_APS_SECURED_LINK_KEY
							    : ZZB_APS_SECURED_NWK_KEY,
	};

	net_buf_pull(buf, hdr_len);
	zzb_apsde_data_ind(node, buf, &ind);
}

void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use)
{
	struct net_buf *ack = NULL;
	struct zzb_aps_hdr hdr;
	struct zzb_aps_sec sec;
	int hdr_len = rx_frame(node, buf, src_addr, security_use, &hdr, &sec);

	if (hdr_len >= 0 && hdr.type == ZZB_APS_FRAME_ACK) {
		zzb_aps_ack_rx(node, &hdr, src_addr);
	}

	if (hdr_len < 0 || hdr.type != ZZB_APS_FRAME_DATA) {
		net_buf_unref(buf);
		return;
	}

	/* Built before the delivery, sent after it (section 11) */
	if ((hdr.flags & ZZB_APS_ACK_REQ) && !zzb_nwk_is_broadcast(dst_addr)) {
		ack = zzb_aps_ack_build(node, &hdr, &sec);
	}

	if (zzb_aps_dup_check(node, src_addr, hdr.counter)) {
		net_buf_unref(buf);
	} else {
		zzb_aps_dup_add(node, src_addr, hdr.counter);
		deliver(node, buf, &hdr, hdr_len, dst_addr, src_addr);
	}

	if (ack != NULL) {
		zzb_aps_send_own(node, ack, src_addr);
	}
}
