/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/eui64.h"
#include "common/node.h"
#include "common/queue.h"
#include "mac/mac.h"
#include "nwk/nwk.h"
#include "sec/install_code.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

static struct zzb_node nodes[CONFIG_ZZB_NODE_COUNT];

struct init_req {
	struct k_work work;
	const struct zzb_node_config *cfg;
	struct zzb_node *node;
	int err;
};

int zzb_node_index(const struct zzb_node *node)
{
	return node - nodes;
}

const struct zzb_endpoint *zzb_node_endpoint(const struct zzb_node *node, uint8_t id)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		if (node->device->endpoints[i].id == id) {
			return &node->device->endpoints[i];
		}
	}

	return NULL;
}

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

/* BDB 10.1.1: a code of 6, 8, 12 or 16 bytes and its CRC */
static int install_key(const struct zzb_node_config *cfg, uint8_t key[ZZB_SEC_KEY_LEN])
{
	size_t code_len = cfg->install_code_len - 2;

	if (code_len != 6 && code_len != 8 && code_len != 12 && code_len != 16) {
		return -EINVAL;
	}

	return zzb_sec_install_code_key(cfg->install_code, cfg->install_code_len, key);
}

static void init_handler(struct k_work *work)
{
	struct init_req *req = CONTAINER_OF(work, struct init_req, work);
	const struct zzb_node_config *cfg = req->cfg;
	uint8_t key[ZZB_SEC_KEY_LEN] = {0};
	struct zzb_node *node;
	uint64_t eui64 = cfg->eui64;

	if ((IS_ENABLED(CONFIG_ZZB_MAC) && cfg->radio == NULL) ||
	    ((IS_ENABLED(CONFIG_ZZB_ZDO) || IS_ENABLED(CONFIG_ZZB_ZCL)) && cfg->device == NULL) ||
	    (cfg->install_code != NULL && install_key(cfg, key) != 0)) {
		req->err = -EINVAL;
		return;
	}

	node = free_node();
	if (node == NULL) {
		req->err = -ENOMEM;
		return;
	}

	if (eui64 == 0) {
		req->err = hw_eui64(cfg, &eui64);
		if (req->err) {
			return;
		}
	}

	*node = (struct zzb_node){
		.eui64 = eui64,
		.has_install_key = cfg->install_code != NULL,
		.device = cfg->device,
		.bdb_cb = cfg->bdb_cb,
		.zcl_cb = cfg->zcl_cb,
		.zcl_effect_cb = cfg->zcl_effect_cb,
		.zcl_cmd_cb = cfg->zcl_cmd_cb,
		.ota_cb = cfg->ota_cb,
		.sleepy = cfg->sleepy,
	};
	memcpy(node->install_key, key, sizeof(key));

	if (IS_ENABLED(CONFIG_ZZB_MAC)) {
		req->err = zzb_mac_init(node, cfg->radio);
		if (req->err) {
			return;
		}
	}

	if (IS_ENABLED(CONFIG_ZZB_NWK)) {
		zzb_nwk_init(node);
	}

	if (IS_ENABLED(CONFIG_ZZB_APS)) {
		zzb_aps_init(node);
	}

	if (IS_ENABLED(CONFIG_ZZB_ZDO)) {
		zzb_zdo_init(node);
	}

	if (IS_ENABLED(CONFIG_ZZB_ZCL)) {
		zzb_zcl_init(node);
	}

	if (IS_ENABLED(CONFIG_ZZB_NWK) || IS_ENABLED(CONFIG_ZZB_APS) ||
	    IS_ENABLED(CONFIG_ZZB_ZCL)) {
		req->err = zzb_settings_load(node);
		if (req->err) {
			return;
		}
	}

	if (IS_ENABLED(CONFIG_ZZB_NWK)) {
		req->err = zzb_fc_start(&node->nwk.fc);
		if (req->err) {
			return;
		}
	}

	if (IS_ENABLED(CONFIG_ZZB_APS)) {
		req->err = zzb_fc_start(&node->aps.fc);
		if (req->err) {
			return;
		}
	}

	if (IS_ENABLED(CONFIG_ZZB_ZCL)) {
		zzb_zcl_start(node);
	}

	/* BDB reads the network that NWK has loaded */
	if (IS_ENABLED(CONFIG_ZZB_BDB)) {
		zzb_bdb_init(node);
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
