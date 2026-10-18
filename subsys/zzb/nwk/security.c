/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>

#include "common/node.h"
#include "nwk/nwk.h"
#include "sec/aux_hdr.h"
#include "sec/ccm.h"
#include "sec/key_cache.h"

static int key_slot(const struct zzb_nib *nib, uint8_t seq)
{
	for (int i = 0; i < ARRAY_SIZE(nib->keys); i++) {
		if (nib->keys[i].valid && nib->keys[i].seq == seq) {
			return i;
		}
	}

	return -ENOENT;
}

int zzb_nwk_secure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len)
{
	const struct zzb_nwk_key *key = &node->nwk.nib.keys[node->nwk.nib.active_key];
	struct zzb_sec_aux_hdr aux = {
		.key_id = ZZB_SEC_KEY_NWK,
		.ext_nonce = true,
		.src_addr = node->eui64,
		.key_seq = key->seq,
	};
	psa_key_id_t id;
	int err;

	if (!key->valid) {
		return -ENOENT;
	}

	if (net_buf_headroom(buf) < ZZB_NWK_AUX_LEN || net_buf_tailroom(buf) < ZZB_SEC_MIC_LEN) {
		return -EMSGSIZE;
	}

	err = zzb_nwk_fc_next(node, &aux.frame_counter);
	if (err) {
		return err;
	}

	err = zzb_sec_key_get(key->key, &id);
	if (err) {
		return err;
	}

	net_buf_push(buf, ZZB_NWK_AUX_LEN);
	memmove(buf->data, buf->data + ZZB_NWK_AUX_LEN, hdr_len);
	zzb_sec_aux_hdr_write(&aux, buf->data + hdr_len);

	err = zzb_sec_frame_encrypt(id, node->eui64, buf->data, hdr_len, buf->len);
	if (err) {
		return err;
	}

	net_buf_add(buf, ZZB_SEC_MIC_LEN);
	return 0;
}

int zzb_nwk_unsecure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len)
{
	struct zzb_nib *nib = &node->nwk.nib;
	struct zzb_nwk_neighbor *nb;
	struct zzb_sec_aux_hdr aux;
	psa_key_id_t id;
	int aux_len;
	int slot;
	int err;

	aux_len = zzb_sec_aux_hdr_parse(buf->data + hdr_len, buf->len - hdr_len, &aux);
	if (aux_len < 0) {
		return aux_len;
	}

	if (aux.key_id != ZZB_SEC_KEY_NWK || !aux.ext_nonce) {
		return -EINVAL;
	}

	slot = key_slot(nib, aux.key_seq);
	if (slot < 0) {
		return slot;
	}

	nb = zzb_nwk_neighbor_find_ext(node, aux.src_addr);
	if (nb == NULL) {
		return -ENOENT;
	}

	if (aux.frame_counter == UINT32_MAX || aux.frame_counter < nb->in_fc[slot]) {
		return -EALREADY;
	}

	err = zzb_sec_key_get(nib->keys[slot].key, &id);
	if (err) {
		return err;
	}

	err = zzb_sec_frame_decrypt(id, aux.src_addr, buf->data, hdr_len, buf->len);
	if (err) {
		return err;
	}

	nb->in_fc[slot] = aux.frame_counter + 1;
	if (slot == nib->newest_key) {
		nib->active_key = slot;
	}

	net_buf_remove_mem(buf, ZZB_SEC_MIC_LEN);
	memmove(buf->data + aux_len, buf->data, hdr_len);
	net_buf_pull(buf, aux_len);
	return 0;
}
