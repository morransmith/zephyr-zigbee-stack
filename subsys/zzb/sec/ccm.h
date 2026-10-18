/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_CCM_H_
#define ZZB_SUBSYS_SEC_CCM_H_

#include <stddef.h>
#include <stdint.h>
#include <psa/crypto.h>

#include "sec/mmo.h"

#define ZZB_SEC_MIC_LEN   4
#define ZZB_SEC_NONCE_LEN 13

/* CCM* with M = mic_len (R22 B.1.2). Encrypts m in place and writes the MIC after it. */
int zzb_sec_ccm_encrypt(psa_key_id_t key, const uint8_t nonce[ZZB_SEC_NONCE_LEN], const uint8_t *a,
			size_t a_len, uint8_t *m, size_t m_len, size_t mic_len);

/* Verifies and decrypts c in place; c_len includes the MIC. Returns -EBADMSG on a MIC mismatch. */
int zzb_sec_ccm_decrypt(psa_key_id_t key, const uint8_t nonce[ZZB_SEC_NONCE_LEN], const uint8_t *a,
			size_t a_len, uint8_t *c, size_t c_len, size_t mic_len);

/*
 * Secures frame[0, len) in place (R22 4.3.1.1, 4.4.1.1). The auxiliary header starts at aux_off,
 * the payload after it is encrypted and the MIC is written at frame + len. src_addr is the
 * extended address of the device that secures the frame.
 */
int zzb_sec_frame_encrypt(psa_key_id_t key, uint64_t src_addr, uint8_t *frame, size_t aux_off,
			  size_t len);

/*
 * Verifies and decrypts frame[0, len) in place (R22 4.3.1.2, 4.4.1.2); len includes the MIC.
 * src_addr is the extended address of the device that secured the frame.
 */
int zzb_sec_frame_decrypt(psa_key_id_t key, uint64_t src_addr, uint8_t *frame, size_t aux_off,
			  size_t len);

#endif /* ZZB_SUBSYS_SEC_CCM_H_ */
