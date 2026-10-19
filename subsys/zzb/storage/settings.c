/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "common/node.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

/* Room for zzb/<n>/nwk/keys */
#define NAME_LEN 24

struct load_ctx {
	struct zzb_node *node;
	int err;
};

static int load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct load_ctx *ctx = param;

	if (settings_name_steq(key, "fc/nwk", NULL)) {
		ctx->err = zzb_fc_load(&ctx->node->nwk.fc, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "nwk/net", NULL)) {
		ctx->err = zzb_nwk_net_load(ctx->node, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "nwk/keys", NULL)) {
		ctx->err = zzb_nwk_keys_load(ctx->node, len, read_cb, cb_arg);
	}

	/* Loading stops at the first error */
	return ctx->err;
}

int zzb_settings_load(struct zzb_node *node)
{
	struct load_ctx ctx = {
		.node = node,
	};
	char subtree[NAME_LEN];
	int err;

	err = settings_subsys_init();
	if (err) {
		return err;
	}

	snprintk(subtree, sizeof(subtree), "zzb/%d", zzb_node_index(node));
	err = settings_load_subtree_direct(subtree, load_cb, &ctx);
	return err ? err : ctx.err;
}

int zzb_settings_save(struct zzb_node *node, const char *key, const void *value, size_t len)
{
	char name[NAME_LEN];

	snprintk(name, sizeof(name), "zzb/%d/%s", zzb_node_index(node), key);
	return settings_save_one(name, value, len);
}

int zzb_settings_delete(struct zzb_node *node, const char *key)
{
	char name[NAME_LEN];

	snprintk(name, sizeof(name), "zzb/%d/%s", zzb_node_index(node), key);
	return settings_delete(name);
}
