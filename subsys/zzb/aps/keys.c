/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "aps/aps.h"
#include "common/node.h"
#include "storage/settings.h"

#define TC_KEY         "aps/tc"
#define RECORD_VERSION 1
#define TC_LEN         9
#define KEY_LEN        27
/* Room for aps/key/<i> with any int i */
#define KEY_NAME_LEN   20

const uint8_t zzb_aps_default_tc_link_key[ZZB_SEC_KEY_LEN] = {
	0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
	0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39,
};

struct zzb_aps_key_pair *zzb_aps_key_pair_find(struct zzb_node *node, uint64_t addr)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.keys, pair) {
		if (pair->in_use && pair->addr == addr) {
			return pair;
		}
	}

	return NULL;
}

/* The entry of addr, or a free entry, or NULL */
static struct zzb_aps_key_pair *entry_for(struct zzb_node *node, uint64_t addr)
{
	struct zzb_aps_key_pair *pair = zzb_aps_key_pair_find(node, addr);

	if (pair != NULL) {
		return pair;
	}

	ARRAY_FOR_EACH_PTR(node->aps.aib.keys, free) {
		if (!free->in_use) {
			return free;
		}
	}

	return NULL;
}

static void key_name(char name[KEY_NAME_LEN], int index)
{
	snprintk(name, KEY_NAME_LEN, "aps/key/%d", index);
}

static void save_tc(struct zzb_node *node)
{
	uint8_t rec[TC_LEN] = {RECORD_VERSION};

	sys_put_le64(node->aps.aib.tc_addr, rec + 1);
	zzb_settings_save(node, TC_KEY, rec, sizeof(rec));
}

static void save_key(struct zzb_node *node, const struct zzb_aps_key_pair *pair)
{
	uint8_t rec[KEY_LEN] = {RECORD_VERSION};
	char name[KEY_NAME_LEN];

	sys_put_le64(pair->addr, rec + 1);
	rec[9] = pair->attr;
	rec[10] = pair->type;
	memcpy(rec + 11, pair->key, ZZB_SEC_KEY_LEN);
	key_name(name, pair - node->aps.aib.keys);
	zzb_settings_save(node, name, rec, sizeof(rec));
}

int zzb_apsme_set_link_key(struct zzb_node *node, uint64_t addr, const uint8_t key[ZZB_SEC_KEY_LEN],
			   enum zzb_aps_key_attr attr, enum zzb_aps_link_key_type type)
{
	struct zzb_aps_key_pair *pair = entry_for(node, addr);

	if (pair == NULL) {
		return -ENOMEM;
	}

	*pair = (struct zzb_aps_key_pair){
		.in_use = true,
		.addr = addr,
		.attr = attr,
		.type = type,
	};
	memcpy(pair->key, key, ZZB_SEC_KEY_LEN);
	save_key(node, pair);
	return 0;
}

int zzb_apsme_set_tc_addr(struct zzb_node *node, uint64_t addr)
{
	int err;

	if (zzb_aps_key_pair_find(node, addr) == NULL) {
		err = zzb_apsme_set_link_key(node, addr, zzb_aps_default_tc_link_key,
					     ZZB_APS_KEY_PROVISIONAL, ZZB_APS_LINK_KEY_GLOBAL);
		if (err) {
			return err;
		}
	}

	node->aps.aib.tc_addr = addr;
	save_tc(node);
	return 0;
}

void zzb_aps_delete(struct zzb_node *node)
{
	char name[KEY_NAME_LEN];

	zzb_settings_delete(node, TC_KEY);
	for (int i = 0; i < ARRAY_SIZE(node->aps.aib.keys); i++) {
		key_name(name, i);
		zzb_settings_delete(node, name);
	}
}

int zzb_aps_tc_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t rec[TC_LEN];
	int err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));

	if (err) {
		return err;
	}

	node->aps.aib.tc_addr = sys_get_le64(rec + 1);
	return 0;
}

int zzb_aps_key_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
		     void *cb_arg)
{
	struct zzb_aps_key_pair *pair;
	uint8_t rec[KEY_LEN];
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->aps.aib.keys)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	pair = &node->aps.aib.keys[index];
	*pair = (struct zzb_aps_key_pair){
		.in_use = true,
		.addr = sys_get_le64(rec + 1),
		.attr = rec[9],
		.type = rec[10],
	};
	memcpy(pair->key, rec + 11, ZZB_SEC_KEY_LEN);
	return 0;
}
