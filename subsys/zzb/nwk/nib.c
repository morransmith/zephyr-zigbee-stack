/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "mac/mac.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

#define NET_KEY        "nwk/net"
#define KEYS_KEY       "nwk/keys"
#define RECORD_VERSION 1
#define NET_LEN        28
#define KEYS_LEN       39

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

/* The slot of the key with sequence number seq, otherwise the slot a new key replaces */
static int write_slot(const struct zzb_nib *nib, uint8_t seq)
{
	int slot = zzb_nwk_key_slot(nib, seq);

	if (slot >= 0) {
		return slot;
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
	int slot = write_slot(nib, seq);

	nib->keys[slot].valid = true;
	nib->keys[slot].seq = seq;
	memcpy(nib->keys[slot].key, key, ZZB_SEC_KEY_LEN);
	nib->newest_key = slot;

	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		nb->in_fc[slot] = 0;
	}

	/* R22 3.6.10.5 */
	if (node->nwk.key_pending) {
		node->nwk.key_pending = false;
		zzb_nwk_ed_timeout_start(node);
	}

	zzb_nwk_save(node);
}

int zzb_nlme_set_active_key(struct zzb_node *node, uint8_t seq)
{
	int slot = zzb_nwk_key_slot(&node->nwk.nib, seq);

	if (slot < 0) {
		return slot;
	}

	node->nwk.nib.active_key = slot;
	zzb_nwk_save(node);
	return 0;
}

int zzb_nlme_set_channel(struct zzb_node *node, uint8_t channel, uint8_t update_id)
{
	int err;

	if (channel < 11 || channel > 26) {
		return -EINVAL;
	}

	if (node->nwk.op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	err = zzb_mlme_set_channel(node, channel);
	if (err) {
		return err;
	}

	node->nwk.nib.channel = channel;
	node->nwk.nib.update_id = update_id;
	zzb_nwk_save(node);
	return 0;
}

uint16_t zzb_nlme_get_short_addr(const struct zzb_node *node)
{
	return node->nwk.nib.short_addr;
}

uint64_t zzb_nlme_get_ext_pan_id(const struct zzb_node *node)
{
	return node->nwk.nib.ext_pan_id;
}

uint8_t zzb_nlme_get_capability(const struct zzb_node *node)
{
	return node->nwk.nib.capability;
}

bool zzb_nwk_key_pending(const struct zzb_node *node)
{
	return node->nwk.key_pending;
}

static void save_net(struct zzb_node *node)
{
	const struct zzb_nib *nib = &node->nwk.nib;
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);
	uint8_t rec[NET_LEN] = {RECORD_VERSION};
	uint8_t *p = rec + 1;

	if (parent == NULL) {
		return;
	}

	sys_put_le16(nib->pan_id, p);
	sys_put_le64(nib->ext_pan_id, p + 2);
	sys_put_le16(nib->short_addr, p + 10);
	p[12] = nib->channel;
	p[13] = nib->update_id;
	p[14] = nib->capability;
	p[15] = nib->parent_info;
	sys_put_le16(parent->short_addr, p + 16);
	sys_put_le64(parent->ext_addr, p + 18);
	p[26] = parent->device_type;
	zzb_settings_save(node, NET_KEY, rec, sizeof(rec));
}

static void save_keys(struct zzb_node *node)
{
	const struct zzb_nib *nib = &node->nwk.nib;
	uint8_t rec[KEYS_LEN] = {RECORD_VERSION, nib->active_key, nib->newest_key};
	uint8_t *p = rec + 3;

	ARRAY_FOR_EACH_PTR(nib->keys, key) {
		p[0] = key->valid;
		p[1] = key->seq;
		memcpy(p + 2, key->key, ZZB_SEC_KEY_LEN);
		p += 2 + ZZB_SEC_KEY_LEN;
	}

	zzb_settings_save(node, KEYS_KEY, rec, sizeof(rec));
}

void zzb_nwk_save(struct zzb_node *node)
{
	if (node->nwk.nib.ext_pan_id == 0 || node->nwk.key_pending) {
		return;
	}

	save_net(node);
	save_keys(node);
}

void zzb_nwk_delete(struct zzb_node *node)
{
	zzb_settings_delete(node, NET_KEY);
	zzb_settings_delete(node, KEYS_KEY);
	zzb_nwk_addr_map_delete(node);
}

int zzb_nwk_net_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct zzb_nib *nib = &node->nwk.nib;
	uint8_t rec[NET_LEN];
	const uint8_t *p = rec + 1;
	struct zzb_nwk_neighbor *parent;
	int err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));

	if (err) {
		return err;
	}

	nib->pan_id = sys_get_le16(p);
	nib->ext_pan_id = sys_get_le64(p + 2);
	nib->short_addr = sys_get_le16(p + 10);
	nib->channel = p[12];
	nib->update_id = p[13];
	nib->capability = p[14];
	nib->parent_info = p[15];

	parent = zzb_nwk_neighbor_add(node, sys_get_le64(p + 18), sys_get_le16(p + 16),
				      ZZB_NWK_REL_PARENT);
	if (parent == NULL) {
		return -ENOMEM;
	}

	/* A rejoin finds the parent by its beacon */
	parent->device_type = p[26];
	parent->beacon.ext_pan_id = nib->ext_pan_id;
	parent->beacon.update_id = nib->update_id;
	parent->pan_id = nib->pan_id;
	parent->channel = nib->channel;

	zzb_mlme_set_pan_id(node, nib->pan_id);
	zzb_mlme_set_short_addr(node, nib->short_addr);
	zzb_mlme_set_channel(node, nib->channel);
	zzb_nwk_keepalive_resume(node);
	zzb_nwk_poll_start(node);
	return 0;
}

int zzb_nwk_keys_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct zzb_nib *nib = &node->nwk.nib;
	uint8_t rec[KEYS_LEN];
	const uint8_t *p = rec + 3;
	int err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));

	if (err) {
		return err;
	}

	nib->active_key = rec[1] & 1;
	nib->newest_key = rec[2] & 1;
	ARRAY_FOR_EACH_PTR(nib->keys, key) {
		key->valid = p[0];
		key->seq = p[1];
		memcpy(key->key, p + 2, ZZB_SEC_KEY_LEN);
		p += 2 + ZZB_SEC_KEY_LEN;
	}

	return 0;
}
