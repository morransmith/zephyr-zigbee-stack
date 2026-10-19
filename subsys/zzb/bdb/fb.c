/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"
#include "zdo/zdo.h"

/* Identify cluster (ZCL 3.5) */
#define CLUSTER_IDENTIFY 0x0003

/* Whether endpoint ep has an instance of cluster */
static bool has_cluster(const struct zzb_node *node, uint8_t ep, uint16_t cluster, bool server)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);

	for (int i = 0; e != NULL && i < e->cluster_count; i++) {
		if (e->clusters[i].id == cluster && e->clusters[i].server == server) {
			return true;
		}
	}

	return false;
}

/* BDB 8.4 */
static int target(struct zzb_node *node, uint8_t ep)
{
	if (!has_cluster(node, ep, CLUSTER_IDENTIFY, true)) {
		return -ENOENT;
	}

	if (node->bdb.fb_target_ep != 0) {
		return -EBUSY;
	}

	/* Step 1 */
	if (!node->bdb.on_network) {
		zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_TARGET,
			       ZZB_BDB_NOT_ON_A_NETWORK);
		return 0;
	}

	/* Step 3; other identifying endpoints are not changed (MAY) */
	node->bdb.fb_target_ep = ep;
	return zzb_zcl_identify(node, ep, ZZB_BDB_MIN_COMMISSIONING_S);
}

/* Step 5 */
void zzb_bdb_identify_end(struct zzb_node *node, uint8_t ep)
{
	if (ep == 0 || ep != node->bdb.fb_target_ep) {
		return;
	}

	node->bdb.fb_target_ep = 0;
	zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_SUCCESS);
}

/* The callbacks of finding & binding for an initiator (BDB 8.5) */
void zzb_bdb_identify_query_rsp(struct zzb_node *node, uint8_t ep, uint16_t addr, uint8_t src_ep)
{
}

void zzb_zdo_simple_desc_conf(struct zzb_node *node, uint8_t status, uint16_t addr,
			      const struct zzb_zdo_simple_desc *desc)
{
}

void zzb_zdo_ieee_addr_conf(struct zzb_node *node, uint8_t status, uint16_t addr, uint64_t ext_addr)
{
}

void zzb_bdb_fb_clear(struct zzb_node *node)
{
	node->bdb.fb_target_ep = 0;
}

struct target_call {
	struct k_work work;
	struct zzb_node *node;
	uint8_t ep;
	int err;
};

static void target_handler(struct k_work *work)
{
	struct target_call *call = CONTAINER_OF(work, struct target_call, work);

	call->err = target(call->node, call->ep);
}

int zzb_bdb_finding_binding_target(struct zzb_node *node, uint8_t ep)
{
	struct target_call call = {
		.node = node,
		.ep = ep,
	};

	zzb_call_sync(&call.work, target_handler);
	return call.err;
}
