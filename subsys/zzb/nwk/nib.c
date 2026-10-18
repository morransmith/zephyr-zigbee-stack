/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "common/node.h"
#include "mac/mac.h"
#include "nwk/nwk.h"

void zzb_nlme_set_pan_id(struct zzb_node *node, uint16_t pan_id)
{
	node->nwk.nib.pan_id = pan_id;
	zzb_mlme_set_pan_id(node, pan_id);
}

void zzb_nlme_set_short_addr(struct zzb_node *node, uint16_t short_addr)
{
	node->nwk.nib.short_addr = short_addr;
	zzb_mlme_set_short_addr(node, short_addr);
}

void zzb_nlme_set_ext_pan_id(struct zzb_node *node, uint64_t ext_pan_id)
{
	node->nwk.nib.ext_pan_id = ext_pan_id;
}

static int key_slot(const struct zzb_nib *nib, uint8_t seq)
{
	if (nib->keys[0].valid && nib->keys[0].seq == seq) {
		return 0;
	}

	if (nib->keys[1].valid && nib->keys[1].seq == seq) {
		return 1;
	}

	/* The first key goes into the active slot */
	if (!nib->keys[nib->active_key].valid) {
		return nib->active_key;
	}

	return !nib->active_key;
}

void zzb_nlme_set_nwk_key(struct zzb_node *node, const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t seq)
{
	struct zzb_nib *nib = &node->nwk.nib;
	int slot = key_slot(nib, seq);

	nib->keys[slot].valid = true;
	nib->keys[slot].seq = seq;
	memcpy(nib->keys[slot].key, key, ZZB_SEC_KEY_LEN);
	nib->newest_key = slot;

	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		nb->in_fc[slot] = 0;
	}
}
