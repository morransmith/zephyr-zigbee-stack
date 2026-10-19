/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "sec/ccm.h"
#include "sec/hmac.h"

static psa_key_id_t import_key(const uint8_t key[ZZB_SEC_KEY_LEN])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t id;

	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_AT_LEAST_THIS_LENGTH_TAG(PSA_ALG_CCM, 4));
	zassert_equal(psa_import_key(&attr, key, ZZB_SEC_KEY_LEN, &id), PSA_SUCCESS);

	return id;
}

/* R22 C.3 and C.4, M = 8 */
static const uint8_t annex_c_key[] = {
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
};
static const uint8_t annex_c_nonce[] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0x03, 0x02, 0x01, 0x00, 0x06,
};
static const uint8_t annex_c_a[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
static const uint8_t annex_c_m[] = {
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
};
static const uint8_t annex_c_c[] = {
	0x1a, 0x55, 0xa3, 0x6a, 0xbb, 0x6c, 0x61, 0x0d, 0x06, 0x6b, 0x33,
	0x75, 0x64, 0x9c, 0xef, 0x10, 0xd4, 0x66, 0x4e, 0xca, 0xd8, 0x54,
	0xa8, 0x0a, 0x89, 0x5c, 0xc1, 0xd8, 0xff, 0x94, 0x69,
};

ZTEST(zzb_ccm, test_annex_c_encrypt)
{
	psa_key_id_t key = import_key(annex_c_key);
	uint8_t buf[sizeof(annex_c_c)];

	memcpy(buf, annex_c_m, sizeof(annex_c_m));

	zassert_ok(zzb_sec_ccm_encrypt(key, annex_c_nonce, annex_c_a, sizeof(annex_c_a), buf,
				       sizeof(annex_c_m), 8));
	zassert_mem_equal(buf, annex_c_c, sizeof(annex_c_c));
	psa_destroy_key(key);
}

ZTEST(zzb_ccm, test_annex_c_decrypt)
{
	psa_key_id_t key = import_key(annex_c_key);
	uint8_t buf[sizeof(annex_c_c)];

	memcpy(buf, annex_c_c, sizeof(annex_c_c));
	zassert_ok(zzb_sec_ccm_decrypt(key, annex_c_nonce, annex_c_a, sizeof(annex_c_a), buf,
				       sizeof(buf), 8));
	zassert_mem_equal(buf, annex_c_m, sizeof(annex_c_m));

	memcpy(buf, annex_c_c, sizeof(annex_c_c));
	buf[sizeof(buf) - 1] ^= 0x01;
	zassert_equal(zzb_sec_ccm_decrypt(key, annex_c_nonce, annex_c_a, sizeof(annex_c_a), buf,
					  sizeof(buf), 8),
		      -EBADMSG);
	psa_destroy_key(key);
}

/* A MIC mismatch leaves the ciphertext, so that another key can be tried on the same frame */
ZTEST(zzb_ccm, test_failed_decryption_keeps_the_ciphertext)
{
	static const uint8_t other_key[ZZB_SEC_KEY_LEN] = {0x01};
	psa_key_id_t wrong = import_key(other_key);
	psa_key_id_t key = import_key(annex_c_key);
	uint8_t buf[sizeof(annex_c_c)];

	memcpy(buf, annex_c_c, sizeof(annex_c_c));
	zassert_equal(zzb_sec_ccm_decrypt(wrong, annex_c_nonce, annex_c_a, sizeof(annex_c_a), buf,
					  sizeof(buf), 8),
		      -EBADMSG);
	zassert_mem_equal(buf, annex_c_c, sizeof(annex_c_c));

	zassert_ok(zzb_sec_ccm_decrypt(key, annex_c_nonce, annex_c_a, sizeof(annex_c_a), buf,
				       sizeof(buf), 8));
	zassert_mem_equal(buf, annex_c_m, sizeof(annex_c_m));
	psa_destroy_key(wrong);
	psa_destroy_key(key);
}

/*
 * The frame has security level 0 on air. Decryption must restore level 5 for the MIC check, and
 * encrypting the payload again must give back the captured bytes, level 0 included.
 */
static void check_captured_frame(psa_key_id_t key, uint64_t src_addr, const uint8_t *frame,
				 size_t len, size_t aux_off, const uint8_t *payload,
				 size_t payload_len)
{
	size_t hdr_len = len - payload_len - ZZB_SEC_MIC_LEN;
	uint8_t buf[ZZB_BUF_SIZE];

	memcpy(buf, frame, len);
	zassert_ok(zzb_sec_frame_decrypt(key, src_addr, buf, aux_off, len));
	zassert_mem_equal(buf + hdr_len, payload, payload_len);

	memcpy(buf, frame, hdr_len);
	memcpy(buf + hdr_len, payload, payload_len);
	zassert_ok(zzb_sec_frame_encrypt(key, src_addr, buf, aux_off, hdr_len + payload_len));
	zassert_mem_equal(buf, frame, len);
}

/* NWK data frame from Ziggurat (see NOTICE), starting at the NWK header */
ZTEST(zzb_ccm, test_captured_nwk_frame)
{
	static const uint8_t nwk_key[] = {
		0xe8, 0x78, 0x5a, 0x1e, 0xd5, 0x99, 0x6b, 0x3e,
		0xf7, 0x15, 0xcb, 0x3f, 0xbd, 0xd6, 0x91, 0x87,
	};
	static const uint8_t frame[] = {
		0x08, 0x02, 0x42, 0x6b, 0x00, 0x00, 0x0f, 0x2e, 0x28, 0x7a, 0x0a, 0x00, 0x00,
		0xa8, 0xef, 0x17, 0x1e, 0x00, 0x4b, 0x12, 0x00, 0x00, 0xf7, 0xa7, 0xe3, 0x7b,
		0x47, 0xad, 0xb4, 0x75, 0x93, 0xc8, 0xa3, 0x75, 0xc9, 0x8b, 0xa6,
	};
	static const uint8_t payload[] = {
		0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9, 0x01, 0x57, 0x01,
	};
	psa_key_id_t key = import_key(nwk_key);

	check_captured_frame(key, 0x00124b001e17efa8ULL, frame, sizeof(frame), 8, payload,
			     sizeof(payload));
	psa_destroy_key(key);
}

/*
 * APS Transport Key command from zigbee-rs (see NOTICE), starting at the APS header. It is secured
 * with the key-transport key derived from the default trust center link key "ZigBeeAlliance09".
 */
ZTEST(zzb_ccm, test_captured_transport_key_frame)
{
	static const uint8_t tc_link_key[] = {
		0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
		0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39,
	};
	static const uint8_t frame[] = {
		0x21, 0x95, 0x30, 0x00, 0x00, 0x00, 0x00, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce,
		0xf4, 0xcc, 0x56, 0x50, 0x5e, 0x07, 0x2d, 0xc5, 0xc1, 0xe8, 0x40, 0xf2, 0xd5, 0xce,
		0x0c, 0xa9, 0x2d, 0x64, 0x23, 0xcc, 0x0c, 0x56, 0xcc, 0xc4, 0xcc, 0x0f, 0x18, 0xa2,
		0xe4, 0x82, 0x88, 0x58, 0x4a, 0x90, 0x3e, 0x00, 0x47, 0x60, 0xf2, 0x5d,
	};
	static const uint8_t payload[] = {
		0x05, 0x01, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe5, 0x01, 0x30, 0x38, 0x9c,
		0x38, 0xc1, 0xa4, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce, 0xf4,
	};
	uint8_t key_transport_key[ZZB_SEC_KEY_LEN];
	psa_key_id_t key;

	zassert_ok(zzb_sec_hmac(tc_link_key, 0x00, key_transport_key));
	key = import_key(key_transport_key);

	check_captured_frame(key, 0xf4ce36c17d3852e1ULL, frame, sizeof(frame), 2, payload,
			     sizeof(payload));
	psa_destroy_key(key);
}

ZTEST_SUITE(zzb_ccm, NULL, NULL, NULL, NULL, NULL);
