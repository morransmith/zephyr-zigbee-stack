/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_AUX_HDR_H_
#define ZZB_SUBSYS_SEC_AUX_HDR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

/* nwkSecurityLevel: ENC-MIC-32 */
#define ZZB_SEC_LEVEL 5

/* Security control field */
#define ZZB_SEC_CTRL_LEVEL     GENMASK(2, 0)
#define ZZB_SEC_CTRL_KEY_ID    GENMASK(4, 3)
#define ZZB_SEC_CTRL_EXT_NONCE BIT(5)

enum zzb_sec_key_id {
	ZZB_SEC_KEY_DATA = 0,
	ZZB_SEC_KEY_NWK = 1,
	ZZB_SEC_KEY_TRANSPORT = 2,
	ZZB_SEC_KEY_LOAD = 3,
};

struct zzb_sec_aux_hdr {
	enum zzb_sec_key_id key_id;
	bool ext_nonce;
	uint32_t frame_counter;
	/* Present when ext_nonce is set */
	uint64_t src_addr;
	/* Present when key_id is ZZB_SEC_KEY_NWK */
	uint8_t key_seq;
};

/* Writes hdr with the security level sub-field set to 0 and returns its length. */
size_t zzb_sec_aux_hdr_write(const struct zzb_sec_aux_hdr *hdr, uint8_t *p);

/* Returns the header length, or -EINVAL if the header is longer than len. */
int zzb_sec_aux_hdr_parse(const uint8_t *p, size_t len, struct zzb_sec_aux_hdr *hdr);

#endif /* ZZB_SUBSYS_SEC_AUX_HDR_H_ */
