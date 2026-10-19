/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* Node_Desc_rsp: status, NWKAddrOfInterest, then the server mask at offset 8 of the descriptor */
#define NODE_DESC_RSP_LEN  16
#define SERVER_MASK_OFFSET (3 + 8)

static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (t->in_use) {
			next = MIN(next, t->deadline);
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zdo.client_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zdo.client_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* Frees the entry, then issues the confirm of its request */
static void finish(struct zzb_node *node, struct zzb_zdo_transaction *t, uint8_t status,
		   uint16_t server_mask)
{
	t->in_use = false;
	schedule(node);
	zzb_zdo_node_desc_conf(node, status, t->dst, server_mask);
}

static void timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zzb_node *node = CONTAINER_OF(dwork, struct zzb_node, zdo.client_timer);
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (t->in_use && t->deadline <= now) {
			finish(node, t, ZZB_ZDP_TIMEOUT, 0);
		}
	}

	schedule(node);
}

void zzb_zdo_client_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zdo.client_timer, timer_handler);
}

static int request(struct zzb_node *node, uint16_t dst, uint16_t cluster, const uint8_t *payload,
		   size_t len)
{
	struct zzb_zdo_transaction *entry = NULL;
	int err;

	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (!t->in_use) {
			entry = t;
			break;
		}
	}

	if (entry == NULL) {
		return -ENOBUFS;
	}

	err = zzb_zdo_send(node, dst, cluster, node->zdo.tsn, payload, len, ZZB_ZDO_HANDLE);
	if (err) {
		return err;
	}

	/* The response or the timeout ends the transaction, so its APSDE-DATA.confirm is unused */
	*entry = (struct zzb_zdo_transaction){
		.in_use = true,
		.tsn = node->zdo.tsn++,
		.cluster = cluster,
		.dst = dst,
		.deadline = k_uptime_get() + ZZB_ZDO_RSP_TIMEOUT_MS,
	};
	schedule(node);
	return 0;
}

int zzb_zdo_node_desc_req(struct zzb_node *node, uint16_t addr)
{
	uint8_t payload[2];

	sys_put_le16(addr, payload);
	return request(node, addr, ZZB_ZDP_NODE_DESC_REQ, payload, sizeof(payload));
}

/* The transaction of a response: the same TSN and cluster, from the destination of the request */
static struct zzb_zdo_transaction *find(struct zzb_node *node, const struct zzb_zdo_req *rsp)
{
	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (t->in_use && t->tsn == rsp->tsn && (t->cluster | ZZB_ZDP_RSP) == rsp->cluster &&
		    t->dst == rsp->src_addr) {
			return t;
		}
	}

	return NULL;
}

int zzb_zdo_device_annce_req(struct zzb_node *node)
{
	uint8_t payload[11];

	sys_put_le16(zzb_nlme_get_short_addr(node), payload);
	sys_put_le64(node->eui64, payload + 2);
	payload[10] = zzb_nlme_get_capability(node);
	return zzb_zdo_send(node, 0xfffd, ZZB_ZDP_DEVICE_ANNCE, node->zdo.tsn++, payload,
			    sizeof(payload), ZZB_ZDO_HANDLE);
}

void zzb_zdo_client(struct zzb_node *node, const struct zzb_zdo_req *rsp)
{
	struct zzb_zdo_transaction *t = find(node, rsp);

	if (t == NULL || rsp->len < 1) {
		return;
	}

	if (rsp->payload[0] != ZZB_ZDP_SUCCESS) {
		finish(node, t, rsp->payload[0], 0);
		return;
	}

	/* A short descriptor leaves the transaction to its timeout */
	if (rsp->len < NODE_DESC_RSP_LEN) {
		return;
	}

	finish(node, t, ZZB_ZDP_SUCCESS, sys_get_le16(rsp->payload + SERVER_MASK_OFFSET));
}
