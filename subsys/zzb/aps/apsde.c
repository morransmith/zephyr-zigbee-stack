/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "common/queue.h"
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

/* Sets the delivery mode and the destination endpoint or group of hdr */
static int resolve_dst(struct zzb_node *node, const struct zzb_apsde_req *req,
		       struct zzb_aps_hdr *hdr, uint16_t *dst, uint64_t *dst_ext)
{
	switch (req->dst_mode) {
	case ZZB_APS_ADDR_GROUP:
		hdr->delivery = ZZB_APS_DELIVERY_GROUP;
		hdr->group = req->dst_addr;
		*dst = 0xfffd;
		return 0;
	case ZZB_APS_ADDR_SHORT:
		hdr->delivery = zzb_nwk_is_broadcast(req->dst_addr) ? ZZB_APS_DELIVERY_BROADCAST
								    : ZZB_APS_DELIVERY_UNICAST;
		hdr->dst_ep = req->dst_ep;
		*dst = req->dst_addr;
		*dst_ext = ext_of(node, req->dst_addr);
		return 0;
	case ZZB_APS_ADDR_EXT:
		hdr->dst_ep = req->dst_ep;
		*dst_ext = req->dst_ext;
		return resolve_ext(node, req->dst_ext, dst);
	default:
		return -ENOTSUP;
	}
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

static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		if (tx->in_use && tx->deadline != 0) {
			next = MIN(next, tx->deadline);
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->aps.timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->aps.timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

static void wait_ack(struct zzb_node *node, struct zzb_aps_tx *tx)
{
	tx->deadline = k_uptime_get() + ZZB_APS_ACK_WAIT_MS;
	schedule(node);
}

/* Secures a copy of the plaintext with the next APS frame counter and sends it */
static int send_copy(struct zzb_node *node, struct zzb_aps_tx *tx)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	int err = 0;

	if (buf == NULL) {
		return -ENOBUFS;
	}

	net_buf_reserve(buf, net_buf_headroom(tx->frame));
	net_buf_add_mem(buf, tx->frame->data, tx->frame->len);
	zzb_frame_meta(buf)->handle = tx - node->aps.tx;

	if (tx->pair != NULL) {
		err = tx->pair->in_use ? zzb_aps_secure(node, buf, tx->hdr_len, tx->pair->key,
							ZZB_SEC_KEY_DATA, tx->ext_nonce)
				       : -ENOENT;
	}

	if (err == 0) {
		err = zzb_nlde_data_req(node, buf, tx->dst, tx->radius, true);
	}

	if (err) {
		net_buf_unref(buf);
		return err;
	}

	tx->nlde_pending = true;
	return 0;
}

/* Issues the confirm; the entry stays until a pending NLDE-DATA.confirm */
static void finish(struct zzb_node *node, struct zzb_aps_tx *tx, uint8_t status)
{
	net_buf_unref(tx->frame);
	tx->frame = NULL;
	tx->deadline = 0;
	tx->done = true;
	tx->in_use = tx->nlde_pending;
	zzb_apsde_data_conf(node, tx->handle, status);
}

static void attempt_failed(struct zzb_node *node, struct zzb_aps_tx *tx)
{
	if (tx->retries == 0) {
		finish(node, tx, tx->status);
		return;
	}

	tx->retries--;
	if (send_copy(node, tx)) {
		wait_ack(node, tx);
	}
}

static void timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zzb_node *node = CONTAINER_OF(dwork, struct zzb_node, aps.timer);
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		if (tx->in_use && tx->deadline != 0 && tx->deadline <= now) {
			tx->deadline = 0;
			attempt_failed(node, tx);
		}
	}

	schedule(node);
}

void zzb_aps_tx_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->aps.timer, timer_handler);
}

int zzb_apsde_data_req(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_req *req)
{
	bool secure = req->tx_options & ZZB_APS_TX_SECURITY;
	bool ack = req->tx_options & ZZB_APS_TX_ACK;
	bool ext_nonce = req->tx_options & ZZB_APS_TX_EXT_NONCE;
	struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_DATA,
		.flags = (secure ? ZZB_APS_SECURITY : 0) | (ack ? ZZB_APS_ACK_REQ : 0),
		.cluster = req->cluster,
		.profile = req->profile,
		.src_ep = req->src_ep,
		.counter = node->aps.counter,
	};
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_aps_key_pair *pair = NULL;
	uint64_t dst_ext = 0;
	struct zzb_aps_tx *tx;
	size_t hdr_len;
	uint16_t dst;
	int err;

	if (req->tx_options & ZZB_APS_TX_FRAGMENT) {
		return -ENOTSUP;
	}

	err = resolve_dst(node, req, &hdr, &dst, &dst_ext);
	if (err) {
		return err;
	}

	/* R22 2.2.8.4.3 */
	if (ack && hdr.delivery != ZZB_APS_DELIVERY_UNICAST) {
		return -EINVAL;
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

	*tx = (struct zzb_aps_tx){
		.handle = meta->handle,
		.hdr_len = hdr_len,
		.pair = pair,
		.ext_nonce = ext_nonce,
		.dst = dst,
		.cluster = req->cluster,
		.dst_ep = req->dst_ep,
		.counter = hdr.counter,
		.radius = req->radius,
		.retries = ZZB_APS_MAX_FRAME_RETRIES,
		.status = ZZB_APS_STATUS_NO_ACK,
	};
	zzb_aps_hdr_write(&hdr, net_buf_push(buf, hdr_len));

	if (ack) {
		tx->frame = buf;
		err = send_copy(node, tx);
		if (err) {
			tx->frame = NULL;
			return err;
		}
	} else {
		if (secure) {
			err = zzb_aps_secure(node, buf, hdr_len, pair->key, ZZB_SEC_KEY_DATA,
					     ext_nonce);
			if (err) {
				return err;
			}
		}

		meta->handle = tx - node->aps.tx;
		err = zzb_nlde_data_req(node, buf, dst, req->radius, true);
		if (err) {
			return err;
		}

		tx->nlde_pending = true;
	}

	tx->in_use = true;
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
	if (!tx->in_use || !tx->nlde_pending) {
		return;
	}

	tx->nlde_pending = false;
	if (tx->done) {
		tx->in_use = false;
		return;
	}

	if (tx->frame == NULL) {
		tx->in_use = false;
		zzb_apsde_data_conf(node, tx->handle, status);
		return;
	}

	if (status == ZZB_MAC_STATUS_SUCCESS) {
		wait_ack(node, tx);
		return;
	}

	tx->status = status;
	attempt_failed(node, tx);
}

/* R22 2.2.8.4.4; APS sends commands without ACK request */
static bool acknowledges(const struct zzb_aps_hdr *hdr, uint16_t src_addr,
			 const struct zzb_aps_tx *tx)
{
	return tx->in_use && tx->frame != NULL && !(hdr->flags & ZZB_APS_ACK_FORMAT) &&
	       hdr->counter == tx->counter && hdr->cluster == tx->cluster &&
	       hdr->src_ep == tx->dst_ep && src_addr == tx->dst;
}

void zzb_aps_ack_rx(struct zzb_node *node, const struct zzb_aps_hdr *hdr, uint16_t src_addr)
{
	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		if (acknowledges(hdr, src_addr, tx)) {
			finish(node, tx, ZZB_APS_STATUS_SUCCESS);
			schedule(node);
			return;
		}
	}
}
