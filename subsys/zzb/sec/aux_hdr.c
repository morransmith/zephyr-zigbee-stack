/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>

#include "sec/aux_hdr.h"

static size_t aux_hdr_len(const struct zzb_sec_aux_hdr *hdr)
{
	return 5 + (hdr->ext_nonce ? 8 : 0) + (hdr->key_id == ZZB_SEC_KEY_NWK ? 1 : 0);
}

size_t zzb_sec_aux_hdr_write(const struct zzb_sec_aux_hdr *hdr, uint8_t *p)
{
	p[0] = FIELD_PREP(ZZB_SEC_CTRL_KEY_ID, hdr->key_id) |
	       (hdr->ext_nonce ? ZZB_SEC_CTRL_EXT_NONCE : 0);
	sys_put_le32(hdr->frame_counter, p + 1);

	if (hdr->ext_nonce) {
		sys_put_le64(hdr->src_addr, p + 5);
	}

	if (hdr->key_id == ZZB_SEC_KEY_NWK) {
		p[aux_hdr_len(hdr) - 1] = hdr->key_seq;
	}

	return aux_hdr_len(hdr);
}

int zzb_sec_aux_hdr_parse(const uint8_t *p, size_t len, struct zzb_sec_aux_hdr *hdr)
{
	if (len < 5) {
		return -EINVAL;
	}

	*hdr = (struct zzb_sec_aux_hdr){
		.key_id = FIELD_GET(ZZB_SEC_CTRL_KEY_ID, p[0]),
		.ext_nonce = p[0] & ZZB_SEC_CTRL_EXT_NONCE,
		.frame_counter = sys_get_le32(p + 1),
	};

	if (len < aux_hdr_len(hdr)) {
		return -EINVAL;
	}

	if (hdr->ext_nonce) {
		hdr->src_addr = sys_get_le64(p + 5);
	}

	if (hdr->key_id == ZZB_SEC_KEY_NWK) {
		hdr->key_seq = p[aux_hdr_len(hdr) - 1];
	}

	return aux_hdr_len(hdr);
}
