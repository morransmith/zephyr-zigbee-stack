/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "sec/aux_hdr.h"
#include "sec/ccm.h"
#include "sec/hmac.h"
#include "sec/key_cache.h"

/* Input of the keyed hash for the key-transport and key-load keys (R22 4.5.3) */
#define HASH_KEY_TRANSPORT 0x00
#define HASH_KEY_LOAD      0x02

static int import_key(const uint8_t link_key[ZZB_SEC_KEY_LEN], enum zzb_sec_key_id key_id,
		      psa_key_id_t *id)
{
	uint8_t input = key_id == ZZB_SEC_KEY_TRANSPORT ? HASH_KEY_TRANSPORT : HASH_KEY_LOAD;
	uint8_t key[ZZB_SEC_KEY_LEN];
	int err;

	if (key_id == ZZB_SEC_KEY_DATA) {
		return zzb_sec_key_get(link_key, id);
	}

	err = zzb_sec_hmac(link_key, input, key);
	if (err) {
		return err;
	}

	return zzb_sec_key_get(key, id);
}

int zzb_aps_secure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len,
		   const uint8_t link_key[ZZB_SEC_KEY_LEN], enum zzb_sec_key_id key_id,
		   bool ext_nonce)
{
	struct zzb_sec_aux_hdr aux = {
		.key_id = key_id,
		.ext_nonce = ext_nonce,
		.src_addr = node->eui64,
	};
	size_t aux_len = zzb_aps_aux_len(ext_nonce);
	psa_key_id_t id;
	int err;

	if (net_buf_headroom(buf) < aux_len || net_buf_tailroom(buf) < ZZB_SEC_MIC_LEN) {
		return -EMSGSIZE;
	}

	err = zzb_fc_next(&node->aps.fc, &aux.frame_counter);
	if (err) {
		return err;
	}

	err = import_key(link_key, key_id, &id);
	if (err) {
		return err;
	}

	net_buf_push(buf, aux_len);
	memmove(buf->data, buf->data + aux_len, hdr_len);
	zzb_sec_aux_hdr_write(&aux, buf->data + hdr_len);

	err = zzb_sec_frame_encrypt(id, node->eui64, buf->data, hdr_len, buf->len);
	if (err) {
		return err;
	}

	net_buf_add(buf, ZZB_SEC_MIC_LEN);
	return 0;
}

/*
 * The sender: without the extended nonce the trust center at 0x0000 (R22 4.6.2.2) or a neighbor,
 * since nwkAddressMap does not exist yet
 */
static int sender(struct zzb_node *node, const struct zzb_sec_aux_hdr *aux, uint16_t nwk_src,
		  uint64_t *addr)
{
	struct zzb_nwk_neighbor *nb;

	if (aux->ext_nonce) {
		*addr = aux->src_addr;
		return 0;
	}

	if (nwk_src == 0x0000 && node->aps.aib.tc_addr != ZZB_APS_TC_UNKNOWN) {
		*addr = node->aps.aib.tc_addr;
		return 0;
	}

	nb = zzb_nwk_neighbor_find_short(node, nwk_src);
	if (nb == NULL || nb->ext_addr == 0) {
		return -ENOENT;
	}

	*addr = nb->ext_addr;
	return 0;
}

/*
 * The entry of the sender, or no entry and the default key while the network key is pending
 * (R22 4.4.1.2 step 2)
 */
static int find_key(struct zzb_node *node, const struct zzb_sec_aux_hdr *aux, uint16_t nwk_src,
		    struct zzb_aps_sec *sec)
{
	int err = sender(node, aux, nwk_src, &sec->src_addr);

	if (err) {
		return err;
	}

	sec->key_id = aux->key_id;
	sec->pair = zzb_aps_key_pair_find(node, sec->src_addr);
	if (sec->pair == NULL && !zzb_nwk_key_pending(node)) {
		return -ENOENT;
	}

	return 0;
}

int zzb_aps_unsecure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len, uint16_t nwk_src,
		     struct zzb_aps_sec *sec)
{
	struct zzb_aps_key_pair *pair;
	struct zzb_sec_aux_hdr aux;
	psa_key_id_t id;
	int aux_len;
	int err;

	aux_len = zzb_sec_aux_hdr_parse(buf->data + hdr_len, buf->len - hdr_len, &aux);
	if (aux_len < 0) {
		return aux_len;
	}

	if (aux.key_id == ZZB_SEC_KEY_NWK) {
		return -EINVAL;
	}

	err = find_key(node, &aux, nwk_src, sec);
	if (err) {
		return err;
	}

	pair = sec->pair;
	if (aux.frame_counter == UINT32_MAX ||
	    (pair != NULL && pair->type == ZZB_APS_LINK_KEY_UNIQUE &&
	     aux.frame_counter < pair->in_fc)) {
		return -EALREADY;
	}

	err = import_key(pair != NULL ? pair->key : zzb_aps_default_tc_link_key, aux.key_id, &id);
	if (err) {
		return err;
	}

	err = zzb_sec_frame_decrypt(id, sec->src_addr, buf->data, hdr_len, buf->len);
	if (err) {
		return err;
	}

	if (pair != NULL) {
		pair->in_fc = aux.frame_counter + 1;
	}

	net_buf_remove_mem(buf, ZZB_SEC_MIC_LEN);
	memmove(buf->data + aux_len, buf->data, hdr_len);
	net_buf_pull(buf, aux_len);
	return 0;
}
