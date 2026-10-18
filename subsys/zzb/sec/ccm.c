/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "common/buf.h"
#include "sec/aux_hdr.h"
#include "sec/ccm.h"

/* Zephyr builds PSA with MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS; input and output must not overlap */

int zzb_sec_ccm_encrypt(psa_key_id_t key, const uint8_t nonce[ZZB_SEC_NONCE_LEN], const uint8_t *a,
			size_t a_len, uint8_t *m, size_t m_len, size_t mic_len)
{
	uint8_t in[ZZB_BUF_SIZE];
	size_t out_len;

	if (m_len > sizeof(in)) {
		return -EINVAL;
	}

	memcpy(in, m, m_len);
	if (psa_aead_encrypt(key, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, mic_len), nonce,
			     ZZB_SEC_NONCE_LEN, a, a_len, in, m_len, m, m_len + mic_len,
			     &out_len) != PSA_SUCCESS) {
		return -EIO;
	}

	return 0;
}

int zzb_sec_ccm_decrypt(psa_key_id_t key, const uint8_t nonce[ZZB_SEC_NONCE_LEN], const uint8_t *a,
			size_t a_len, uint8_t *c, size_t c_len, size_t mic_len)
{
	uint8_t in[ZZB_BUF_SIZE];
	psa_status_t status;
	size_t out_len;

	if (c_len < mic_len || c_len > sizeof(in)) {
		return -EINVAL;
	}

	memcpy(in, c, c_len);
	status = psa_aead_decrypt(key, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, mic_len), nonce,
				  ZZB_SEC_NONCE_LEN, a, a_len, in, c_len, c, c_len - mic_len,
				  &out_len);
	if (status == PSA_ERROR_INVALID_SIGNATURE) {
		return -EBADMSG;
	}

	return status == PSA_SUCCESS ? 0 : -EIO;
}

/*
 * Sets the security level sub-field to 5 and builds the nonce (R22 4.5.2.2). Returns the length
 * of the header up to the end of the auxiliary header.
 */
static int frame_nonce(uint64_t src_addr, uint8_t *frame, size_t aux_off, size_t len,
		       uint8_t nonce[ZZB_SEC_NONCE_LEN])
{
	struct zzb_sec_aux_hdr hdr;
	int aux_len;

	if (aux_off > len) {
		return -EINVAL;
	}

	aux_len = zzb_sec_aux_hdr_parse(frame + aux_off, len - aux_off, &hdr);
	if (aux_len < 0) {
		return aux_len;
	}

	frame[aux_off] = (frame[aux_off] & ~ZZB_SEC_CTRL_LEVEL) | ZZB_SEC_LEVEL;

	sys_put_le64(src_addr, nonce);
	sys_put_le32(hdr.frame_counter, nonce + 8);
	nonce[12] = frame[aux_off];

	return aux_off + aux_len;
}

int zzb_sec_frame_encrypt(psa_key_id_t key, uint64_t src_addr, uint8_t *frame, size_t aux_off,
			  size_t len)
{
	uint8_t nonce[ZZB_SEC_NONCE_LEN];
	int hdr_len = frame_nonce(src_addr, frame, aux_off, len, nonce);
	int err;

	if (hdr_len < 0) {
		return hdr_len;
	}

	err = zzb_sec_ccm_encrypt(key, nonce, frame, hdr_len, frame + hdr_len, len - hdr_len,
				  ZZB_SEC_MIC_LEN);

	/* R22 4.3.1.1 step 8, 4.4.1.1 step 10 */
	frame[aux_off] &= ~ZZB_SEC_CTRL_LEVEL;

	return err;
}

int zzb_sec_frame_decrypt(psa_key_id_t key, uint64_t src_addr, uint8_t *frame, size_t aux_off,
			  size_t len)
{
	uint8_t nonce[ZZB_SEC_NONCE_LEN];
	int hdr_len = frame_nonce(src_addr, frame, aux_off, len, nonce);

	if (hdr_len < 0) {
		return hdr_len;
	}

	return zzb_sec_ccm_decrypt(key, nonce, frame, hdr_len, frame + hdr_len, len - hdr_len,
				   ZZB_SEC_MIC_LEN);
}
