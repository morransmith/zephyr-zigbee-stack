/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "common/node.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

#define RECORD_VERSION 1
#define RECORD_LEN     11
/* Room for nwk/addr/<i> with any int i */
#define NAME_LEN       21

static void record_name(char name[NAME_LEN], int index)
{
	snprintk(name, NAME_LEN, "nwk/addr/%d", index);
}

static struct zzb_nwk_addr_map *find(struct zzb_node *node, uint64_t ext_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.nib.addr_map, entry) {
		if (entry->in_use && entry->ext_addr == ext_addr) {
			return entry;
		}
	}

	return NULL;
}

/* The entry of ext_addr, or a free entry, or NULL */
static struct zzb_nwk_addr_map *entry_for(struct zzb_node *node, uint64_t ext_addr)
{
	struct zzb_nwk_addr_map *entry = find(node, ext_addr);

	if (entry != NULL) {
		return entry;
	}

	ARRAY_FOR_EACH_PTR(node->nwk.nib.addr_map, free) {
		if (!free->in_use) {
			return free;
		}
	}

	return NULL;
}

int zzb_nlme_set_addr_map(struct zzb_node *node, uint64_t ext_addr, uint16_t short_addr)
{
	struct zzb_nwk_addr_map *entry = entry_for(node, ext_addr);
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION};
	char name[NAME_LEN];

	if (entry == NULL) {
		return -ENOMEM;
	}

	*entry = (struct zzb_nwk_addr_map){
		.in_use = true,
		.ext_addr = ext_addr,
		.short_addr = short_addr,
	};
	sys_put_le64(ext_addr, rec + 1);
	sys_put_le16(short_addr, rec + 9);
	record_name(name, entry - node->nwk.nib.addr_map);
	zzb_settings_save(node, name, rec, sizeof(rec));
	return 0;
}

int zzb_nwk_addr_map_find(struct zzb_node *node, uint64_t ext_addr, uint16_t *short_addr)
{
	const struct zzb_nwk_addr_map *entry = find(node, ext_addr);

	if (entry == NULL) {
		return -ENOENT;
	}

	*short_addr = entry->short_addr;
	return 0;
}

uint64_t zzb_nwk_addr_map_ext(struct zzb_node *node, uint16_t short_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.nib.addr_map, entry) {
		if (entry->in_use && entry->short_addr == short_addr) {
			return entry->ext_addr;
		}
	}

	return 0;
}

void zzb_nwk_addr_map_delete(struct zzb_node *node)
{
	char name[NAME_LEN];

	for (int i = 0; i < ARRAY_SIZE(node->nwk.nib.addr_map); i++) {
		record_name(name, i);
		zzb_settings_delete(node, name);
	}
}

int zzb_nwk_addr_map_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			  void *cb_arg)
{
	uint8_t rec[RECORD_LEN];
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->nwk.nib.addr_map)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	node->nwk.nib.addr_map[index] = (struct zzb_nwk_addr_map){
		.in_use = true,
		.ext_addr = sys_get_le64(rec + 1),
		.short_addr = sys_get_le16(rec + 9),
	};
	return 0;
}
