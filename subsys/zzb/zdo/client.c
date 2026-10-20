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

/* IEEE_addr_rsp of request type 0x00: status, IEEE address and network address */
#define IEEE_ADDR_RSP_LEN 11

/* Simple_Desc_rsp: status, NWKAddrOfInterest and length, then the descriptor */
#define SIMPLE_DESC_OFFSET 4
/* Endpoint, profile, device ID, device version and input cluster count */
#define SIMPLE_DESC_MIN    7

static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (t->in_use) {
			next = MIN(next, t->deadline);
		}
	}

	/* A sleepy node fetches the response from its parent */
	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_ZDO, next != INT64_MAX);
	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zdo.client_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zdo.client_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* Parses the descriptor of a Simple_Desc_rsp with SUCCESS; false when it is short */
static bool simple_desc(const struct zzb_zdo_req *rsp, struct zzb_zdo_simple_desc *desc)
{
	const uint8_t *p = rsp->payload + SIMPLE_DESC_OFFSET;
	size_t len;
	size_t out;

	if (rsp->len < SIMPLE_DESC_OFFSET) {
		return false;
	}

	len = rsp->payload[3];
	if (len < SIMPLE_DESC_MIN || rsp->len < SIMPLE_DESC_OFFSET + len) {
		return false;
	}

	/* The output cluster count follows the input cluster list */
	out = SIMPLE_DESC_MIN + 2 * p[6];
	if (len < out + 1 || len < out + 1 + 2 * p[out]) {
		return false;
	}

	*desc = (struct zzb_zdo_simple_desc){
		.ep = p[0],
		.profile = sys_get_le16(p + 1),
		.device_id = sys_get_le16(p + 3),
		.in_count = p[6],
		.in = p + SIMPLE_DESC_MIN,
		.out_count = p[out],
		.out = p + out + 1,
	};
	return true;
}

/* Whether a response with SUCCESS holds everything its cluster needs */
static bool complete(uint16_t cluster, const struct zzb_zdo_req *rsp)
{
	struct zzb_zdo_simple_desc desc;

	switch (cluster) {
	case ZZB_ZDP_NODE_DESC_REQ:
		return rsp->len >= NODE_DESC_RSP_LEN;
	case ZZB_ZDP_IEEE_ADDR_REQ:
		return rsp->len >= IEEE_ADDR_RSP_LEN;
	default:
		return simple_desc(rsp, &desc);
	}
}

/*
 * Frees the entry, then issues the confirm of its request with the response, NULL after the
 * timeout
 */
static void finish(struct zzb_node *node, struct zzb_zdo_transaction *t,
		   const struct zzb_zdo_req *rsp)
{
	uint8_t status = rsp != NULL ? rsp->payload[0] : ZZB_ZDP_TIMEOUT;
	bool ok = status == ZZB_ZDP_SUCCESS;
	struct zzb_zdo_simple_desc desc;
	uint64_t ext_addr;

	t->in_use = false;
	schedule(node);
	switch (t->cluster) {
	case ZZB_ZDP_NODE_DESC_REQ:
		zzb_zdo_node_desc_conf(node, status, t->dst,
				       ok ? sys_get_le16(rsp->payload + SERVER_MASK_OFFSET) : 0);
		break;
	case ZZB_ZDP_IEEE_ADDR_REQ:
		ext_addr = ok ? sys_get_le64(rsp->payload + 1) : 0;
		if (ok) {
			zzb_nlme_set_addr_map(node, ext_addr, sys_get_le16(rsp->payload + 9));
		}

		zzb_zdo_ieee_addr_conf(node, status, t->dst, ext_addr);
		break;
	default:
		zzb_zdo_simple_desc_conf(node, status, t->dst,
					 ok && simple_desc(rsp, &desc) ? &desc : NULL);
		break;
	}
}

static void timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zzb_node *node = CONTAINER_OF(dwork, struct zzb_node, zdo.client_timer);
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->zdo.transactions, t) {
		if (t->in_use && t->deadline <= now) {
			finish(node, t, NULL);
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

int zzb_zdo_simple_desc_req(struct zzb_node *node, uint16_t addr, uint8_t ep)
{
	uint8_t payload[3];

	sys_put_le16(addr, payload);
	payload[2] = ep;
	return request(node, addr, ZZB_ZDP_SIMPLE_DESC_REQ, payload, sizeof(payload));
}

/* Request type 0x00 (single device response) and start index 0 */
int zzb_zdo_ieee_addr_req(struct zzb_node *node, uint16_t addr)
{
	uint8_t payload[4] = {0};

	sys_put_le16(addr, payload);
	return request(node, addr, ZZB_ZDP_IEEE_ADDR_REQ, payload, sizeof(payload));
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

int zzb_zdo_mgmt_permit_joining_req(struct zzb_node *node, uint8_t duration, bool tc_significance)
{
	const uint8_t payload[] = {duration, tc_significance};

	return zzb_zdo_send(node, 0xfffc, ZZB_ZDP_MGMT_PERMIT_JOINING_REQ, node->zdo.tsn++, payload,
			    sizeof(payload), ZZB_ZDO_HANDLE);
}

void zzb_zdo_client(struct zzb_node *node, const struct zzb_zdo_req *rsp)
{
	struct zzb_zdo_transaction *t = find(node, rsp);

	if (t == NULL || rsp->len < 1) {
		return;
	}

	/* A short response with SUCCESS leaves the transaction to its timeout */
	if (rsp->payload[0] == ZZB_ZDP_SUCCESS && !complete(t->cluster, rsp)) {
		return;
	}

	finish(node, t, rsp);
}
