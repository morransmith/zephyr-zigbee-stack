/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "aps/aps.h"
#include "common/node.h"
#include "storage/settings.h"

#define RECORD_VERSION 1
#define RECORD_LEN     14
/* Room for aps/bind/<i> with any int i */
#define NAME_LEN       21

static void record_name(char name[NAME_LEN], int index)
{
	snprintk(name, NAME_LEN, "aps/bind/%d", index);
}

static void save(struct zzb_node *node, const struct zzb_aps_binding *entry)
{
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION, entry->src_ep};
	char name[NAME_LEN];

	sys_put_le16(entry->cluster, rec + 2);
	rec[4] = entry->dst_mode;
	sys_put_le64(entry->dst_mode == ZZB_APS_ADDR_GROUP ? entry->dst_addr : entry->dst_ext,
		     rec + 5);
	rec[13] = entry->dst_ep;
	record_name(name, entry - node->aps.aib.bindings);
	zzb_settings_save(node, name, rec, sizeof(rec));
}

static void delete(struct zzb_node *node, int index)
{
	char name[NAME_LEN];

	record_name(name, index);
	zzb_settings_delete(node, name);
}

static bool same(const struct zzb_aps_binding *a, const struct zzb_aps_binding *b)
{
	if (a->src_ep != b->src_ep || a->cluster != b->cluster || a->dst_mode != b->dst_mode) {
		return false;
	}

	if (a->dst_mode == ZZB_APS_ADDR_GROUP) {
		return a->dst_addr == b->dst_addr;
	}

	return a->dst_ext == b->dst_ext && a->dst_ep == b->dst_ep;
}

static struct zzb_aps_binding *find(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		if (entry->in_use && same(entry, binding)) {
			return entry;
		}
	}

	return NULL;
}

int zzb_apsme_bind_req(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	if (binding->dst_mode != ZZB_APS_ADDR_GROUP && binding->dst_mode != ZZB_APS_ADDR_EXT) {
		return -EINVAL;
	}

	if (find(node, binding) != NULL) {
		return 0;
	}

	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		if (!entry->in_use) {
			*entry = *binding;
			entry->in_use = true;
			save(node, entry);
			return 0;
		}
	}

	return -ENOMEM;
}

int zzb_apsme_unbind_req(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	struct zzb_aps_binding *entry = find(node, binding);

	if (entry == NULL) {
		return -ENOENT;
	}

	entry->in_use = false;
	delete(node, entry - node->aps.aib.bindings);
	return 0;
}

bool zzb_aps_bound(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t addr,
		   uint8_t src_ep)
{
	uint16_t dst;

	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		if (!entry->in_use || entry->dst_mode != ZZB_APS_ADDR_EXT || entry->src_ep != ep ||
		    entry->cluster != cluster || entry->dst_ep != src_ep) {
			continue;
		}

		if (zzb_aps_resolve_ext(node, entry->dst_ext, &dst) == 0 && dst == addr) {
			return true;
		}
	}

	return false;
}

void zzb_aps_binding_delete(struct zzb_node *node)
{
	for (int i = 0; i < ARRAY_SIZE(node->aps.aib.bindings); i++) {
		delete(node, i);
	}
}

int zzb_aps_binding_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			 void *cb_arg)
{
	struct zzb_aps_binding *entry;
	uint8_t rec[RECORD_LEN];
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->aps.aib.bindings)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	entry = &node->aps.aib.bindings[index];
	*entry = (struct zzb_aps_binding){
		.in_use = true,
		.src_ep = rec[1],
		.cluster = sys_get_le16(rec + 2),
		.dst_mode = rec[4],
		.dst_addr = sys_get_le16(rec + 5),
		.dst_ext = sys_get_le64(rec + 5),
		.dst_ep = rec[13],
	};
	return 0;
}
