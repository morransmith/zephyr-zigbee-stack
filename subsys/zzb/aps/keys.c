/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "aps/aps.h"
#include "common/node.h"

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
	return 0;
}
