/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zzb/zzb.h>

#include "common/eui64.h"
#include "common/node.h"
#include "common/queue.h"

static struct zzb_node nodes[CONFIG_ZZB_NODE_COUNT];

struct init_req {
	struct k_work work;
	const struct zzb_node_config *cfg;
	struct zzb_node *node;
	int err;
};

static struct zzb_node *free_node(void)
{
	ARRAY_FOR_EACH_PTR(nodes, node) {
		if (!node->in_use) {
			return node;
		}
	}

	return NULL;
}

static void init_handler(struct k_work *work)
{
	struct init_req *req = CONTAINER_OF(work, struct init_req, work);
	struct zzb_node *node = free_node();
	uint64_t eui64 = req->cfg->eui64;

	if (node == NULL) {
		req->err = -ENOMEM;
		return;
	}

	if (eui64 == 0) {
		req->err = zzb_eui64_get(&eui64);
		if (req->err) {
			return;
		}
	}

	*node = (struct zzb_node){
		.in_use = true,
		.eui64 = eui64,
	};
	req->node = node;
}

int zzb_node_init(const struct zzb_node_config *cfg, struct zzb_node **node)
{
	struct init_req req = {
		.cfg = cfg,
	};

	zzb_call_sync(&req.work, init_handler);
	*node = req.node;
	return req.err;
}
