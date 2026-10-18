/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "sec/aux_hdr.h"

static void check_round_trip(const uint8_t *bytes, size_t len,
			     const struct zzb_sec_aux_hdr *expected)
{
	struct zzb_sec_aux_hdr hdr;
	uint8_t buf[14];

	zassert_equal(zzb_sec_aux_hdr_parse(bytes, len, &hdr), len);
	zassert_equal(hdr.key_id, expected->key_id);
	zassert_equal(hdr.ext_nonce, expected->ext_nonce);
	zassert_equal(hdr.frame_counter, expected->frame_counter);
	zassert_equal(hdr.src_addr, expected->src_addr);
	zassert_equal(hdr.key_seq, expected->key_seq);

	zassert_equal(zzb_sec_aux_hdr_write(&hdr, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* Auxiliary header of the Ziggurat NWK frame (see NOTICE) */
static const uint8_t nwk_hdr[] = {
	0x28, 0x7a, 0x0a, 0x00, 0x00, 0xa8, 0xef, 0x17, 0x1e, 0x00, 0x4b, 0x12, 0x00, 0x00,
};

ZTEST(zzb_aux_hdr, test_network_key_header)
{
	const struct zzb_sec_aux_hdr expected = {
		.key_id = ZZB_SEC_KEY_NWK,
		.ext_nonce = true,
		.frame_counter = 2682,
		.src_addr = 0x00124b001e17efa8ULL,
		.key_seq = 0,
	};

	check_round_trip(nwk_hdr, sizeof(nwk_hdr), &expected);
}

/* Auxiliary header of the zigbee-rs APS Transport Key frame (see NOTICE) */
ZTEST(zzb_aux_hdr, test_key_transport_key_header)
{
	static const uint8_t bytes[] = {
		0x30, 0x00, 0x00, 0x00, 0x00, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce, 0xf4,
	};
	const struct zzb_sec_aux_hdr expected = {
		.key_id = ZZB_SEC_KEY_TRANSPORT,
		.ext_nonce = true,
		.frame_counter = 0,
		.src_addr = 0xf4ce36c17d3852e1ULL,
	};

	check_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aux_hdr, test_data_key_header_without_address)
{
	static const uint8_t bytes[] = {0x00, 0x01, 0x02, 0x03, 0x04};
	const struct zzb_sec_aux_hdr expected = {
		.key_id = ZZB_SEC_KEY_DATA,
		.frame_counter = 0x04030201,
	};

	check_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aux_hdr, test_truncated_header_is_rejected)
{
	struct zzb_sec_aux_hdr hdr;

	zassert_equal(zzb_sec_aux_hdr_parse(nwk_hdr, sizeof(nwk_hdr) - 1, &hdr), -EINVAL);
	zassert_equal(zzb_sec_aux_hdr_parse(nwk_hdr, 4, &hdr), -EINVAL);
}

ZTEST_SUITE(zzb_aux_hdr, NULL, NULL, NULL, NULL, NULL);
