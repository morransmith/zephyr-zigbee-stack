/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zzb/zzb.h>

#include "common/eui64.h"
#include "common/node.h"
#include "common/queue.h"
#include "mac/mac.h"

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

/* The link address of the radio comes before the hardware sources */
static int hw_eui64(const struct zzb_node_config *cfg, uint64_t *eui64)
{
	if (IS_ENABLED(CONFIG_ZZB_MAC) && zzb_radio_eui64(cfg->radio, eui64) == 0) {
		return 0;
	}

	return zzb_eui64_get(eui64);
}

static void init_handler(struct k_work *work)
{
	struct init_req *req = CONTAINER_OF(work, struct init_req, work);
	struct zzb_node *node;
	uint64_t eui64 = req->cfg->eui64;

	if (IS_ENABLED(CONFIG_ZZB_MAC) && req->cfg->radio == NULL) {
		req->err = -EINVAL;
		return;
	}

	node = free_node();
	if (node == NULL) {
		req->err = -ENOMEM;
		return;
	}

	if (eui64 == 0) {
		req->err = hw_eui64(req->cfg, &eui64);
		if (req->err) {
			return;
		}
	}

	*node = (struct zzb_node){
		.eui64 = eui64,
	};

	if (IS_ENABLED(CONFIG_ZZB_MAC)) {
		req->err = zzb_mac_init(node, req->cfg->radio);
		if (req->err) {
			return;
		}
	}

	node->in_use = true;
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
