/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "nwk/nwk.h"

/* The trust center is at 0x0000 (R22 4.6.2.2); other devices must be neighbors */
static int resolve_ext(struct zzb_node *node, uint64_t ext_addr, uint16_t *addr)
{
	struct zzb_nwk_neighbor *nb;

	if (ext_addr == node->aps.aib.tc_addr) {
		*addr = 0x0000;
		return 0;
	}

	nb = zzb_nwk_neighbor_find_ext(node, ext_addr);
	if (nb == NULL || nb->relationship == ZZB_NWK_REL_NONE) {
		return -ENOENT;
	}

	*addr = nb->short_addr;
	return 0;
}

/* The IEEE address of a network address, or 0 */
static uint64_t ext_of(struct zzb_node *node, uint16_t addr)
{
	struct zzb_nwk_neighbor *nb;

	if (addr == 0x0000) {
		return node->aps.aib.tc_addr;
	}

	nb = zzb_nwk_neighbor_find_short(node, addr);
	return nb != NULL ? nb->ext_addr : 0;
}

static struct zzb_aps_tx *free_tx(struct zzb_node *node)
{
	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		if (!tx->in_use) {
			return tx;
		}
	}

	return NULL;
}

int zzb_apsde_data_req(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_req *req)
{
	bool secure = req->tx_options & ZZB_APS_TX_SECURITY;
	bool ext_nonce = req->tx_options & ZZB_APS_TX_EXT_NONCE;
	struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_DATA,
		.flags = secure ? ZZB_APS_SECURITY : 0,
		.dst_ep = req->dst_ep,
		.group = req->dst_addr,
		.cluster = req->cluster,
		.profile = req->profile,
		.src_ep = req->src_ep,
		.counter = node->aps.counter,
	};
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_aps_key_pair *pair = NULL;
	uint64_t dst_ext = req->dst_ext;
	uint16_t dst = req->dst_addr;
	uint8_t handle = meta->handle;
	struct zzb_aps_tx *tx;
	size_t hdr_len;
	int err;

	if (req->tx_options & ZZB_APS_TX_FRAGMENT) {
		return -ENOTSUP;
	}

	switch (req->dst_mode) {
	case ZZB_APS_ADDR_GROUP:
		hdr.delivery = ZZB_APS_DELIVERY_GROUP;
		dst = 0xfffd;
		break;
	case ZZB_APS_ADDR_SHORT:
		hdr.delivery = zzb_nwk_is_broadcast(dst) ? ZZB_APS_DELIVERY_BROADCAST
							 : ZZB_APS_DELIVERY_UNICAST;
		dst_ext = ext_of(node, dst);
		break;
	case ZZB_APS_ADDR_EXT:
		err = resolve_ext(node, dst_ext, &dst);
		if (err) {
			return err;
		}
		break;
	default:
		return -ENOTSUP;
	}

	if (req->tx_options & ZZB_APS_TX_ACK) {
		return hdr.delivery == ZZB_APS_DELIVERY_UNICAST ? -ENOTSUP : -EINVAL;
	}

	if (secure) {
		pair = zzb_aps_key_pair_find(node, dst_ext);
		if (pair == NULL || pair->attr == ZZB_APS_KEY_UNVERIFIED) {
			return -ENOENT;
		}
	}

	hdr_len = zzb_aps_hdr_len(&hdr);
	if (net_buf_headroom(buf) <
	    ZZB_NLDE_HEADROOM + hdr_len + (secure ? zzb_aps_aux_len(ext_nonce) : 0)) {
		return -EMSGSIZE;
	}

	tx = free_tx(node);
	if (tx == NULL) {
		return -ENOBUFS;
	}

	zzb_aps_hdr_write(&hdr, net_buf_push(buf, hdr_len));
	if (secure) {
		err = zzb_aps_secure(node, buf, hdr_len, pair->key, ZZB_SEC_KEY_DATA, ext_nonce);
		if (err) {
			return err;
		}
	}

	meta->handle = tx - node->aps.tx;
	err = zzb_nlde_data_req(node, buf, dst, req->radius, true);
	if (err) {
		return err;
	}

	*tx = (struct zzb_aps_tx){
		.in_use = true,
		.handle = handle,
	};
	node->aps.counter++;
	return 0;
}

void zzb_nlde_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
	struct zzb_aps_tx *tx;

	if (handle >= ARRAY_SIZE(node->aps.tx)) {
		return;
	}

	tx = &node->aps.tx[handle];
	if (!tx->in_use) {
		return;
	}

	tx->in_use = false;
	zzb_apsde_data_conf(node, tx->handle, status);
}
