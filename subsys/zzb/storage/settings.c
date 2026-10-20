/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "aps/aps.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* Room for zzb/<n>/aps/bind/<i> and zzb/<n>/zcl/<ep>/<cluster>/<attr> */
#define NAME_LEN 32

struct load_ctx {
	struct zzb_node *node;
	int err;
};

static void load_nwk(struct load_ctx *ctx, const char *key, size_t len, settings_read_cb read_cb,
		     void *cb_arg)
{
	const char *index;

	if (settings_name_steq(key, "fc/nwk", NULL)) {
		ctx->err = zzb_fc_load(&ctx->node->nwk.fc, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "nwk/net", NULL)) {
		ctx->err = zzb_nwk_net_load(ctx->node, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "nwk/keys", NULL)) {
		ctx->err = zzb_nwk_keys_load(ctx->node, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "nwk/addr", &index) && index != NULL) {
		ctx->err = zzb_nwk_addr_map_load(ctx->node, atoi(index), len, read_cb, cb_arg);
	}
}

static void load_aps(struct load_ctx *ctx, const char *key, size_t len, settings_read_cb read_cb,
		     void *cb_arg)
{
	const char *index;

	if (settings_name_steq(key, "fc/aps", NULL)) {
		ctx->err = zzb_fc_load(&ctx->node->aps.fc, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "aps/tc", NULL)) {
		ctx->err = zzb_aps_tc_load(ctx->node, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "aps/key", &index) && index != NULL) {
		ctx->err = zzb_aps_key_load(ctx->node, atoi(index), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "aps/bind", &index) && index != NULL) {
		ctx->err = zzb_aps_binding_load(ctx->node, atoi(index), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "aps/group", &index) && index != NULL) {
		ctx->err = zzb_aps_group_load(ctx->node, atoi(index), len, read_cb, cb_arg);
	}
}

static void load_zcl(struct load_ctx *ctx, const char *key, size_t len, settings_read_cb read_cb,
		     void *cb_arg)
{
	const char *next;

	if (settings_name_steq(key, "zcl/rep", &next) && next != NULL) {
		ctx->err = zzb_zcl_report_load(ctx->node, atoi(next), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "zcl/scene", &next) && next != NULL) {
		ctx->err = zzb_zcl_scenes_load(ctx->node, atoi(next), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "zcl/alarm", &next) && next != NULL) {
		ctx->err = zzb_zcl_alarms_load(ctx->node, atoi(next), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "zcl/sched", &next) && next != NULL) {
		ctx->err = zzb_zcl_schedule_load(ctx->node, next, len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "zcl/ias", &next) && next != NULL) {
		ctx->err = zzb_zcl_ias_zone_load(ctx->node, atoi(next), len, read_cb, cb_arg);
	} else if (settings_name_steq(key, "zcl", &next) && next != NULL) {
		ctx->err = zzb_zcl_attr_load(ctx->node, next, len, read_cb, cb_arg);
	}
}

static int load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct load_ctx *ctx = param;

	if (IS_ENABLED(CONFIG_ZZB_NWK)) {
		load_nwk(ctx, key, len, read_cb, cb_arg);
	}

	if (IS_ENABLED(CONFIG_ZZB_APS)) {
		load_aps(ctx, key, len, read_cb, cb_arg);
	}

	if (IS_ENABLED(CONFIG_ZZB_ZCL)) {
		load_zcl(ctx, key, len, read_cb, cb_arg);
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

int zzb_settings_read(size_t len, settings_read_cb read_cb, void *cb_arg, uint8_t *rec,
		      size_t rec_len)
{
	if (len != rec_len || read_cb(cb_arg, rec, rec_len) != rec_len || rec[0] != 1) {
		return -EINVAL;
	}

	return 0;
}

int zzb_settings_delete(struct zzb_node *node, const char *key)
{
	char name[NAME_LEN];

	snprintk(name, sizeof(name), "zzb/%d/%s", zzb_node_index(node), key);
	return settings_delete(name);
}
