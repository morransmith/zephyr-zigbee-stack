/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "zcl/frame.h"

static void check_hdr_round_trip(const uint8_t *bytes, size_t len,
				 const struct zzb_zcl_hdr *expected)
{
	struct zzb_zcl_hdr hdr;
	uint8_t buf[ZZB_ZCL_HDR_MAX_LEN];

	zassert_equal(zzb_zcl_hdr_parse(bytes, len, &hdr), len);
	zassert_equal(hdr.type, expected->type);
	zassert_equal(hdr.flags, expected->flags);
	zassert_equal(hdr.manufacturer, expected->manufacturer);
	zassert_equal(hdr.tsn, expected->tsn);
	zassert_equal(hdr.cmd, expected->cmd);

	zassert_equal(zzb_zcl_hdr_len(&hdr), len);
	zassert_equal(zzb_zcl_hdr_write(&hdr, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* The Read Attributes request of zigpy for the Basic cluster in the interview */
ZTEST(zzb_zcl_frame, test_global_header)
{
	static const uint8_t bytes[] = {0x00, 0x04, 0x00};
	const struct zzb_zcl_hdr expected = {
		.type = ZZB_ZCL_FRAME_GLOBAL,
		.tsn = 0x04,
		.cmd = 0x00,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

/* ZCL 2.4.1: the manufacturer code follows the frame control field */
ZTEST(zzb_zcl_frame, test_manufacturer_header)
{
	static const uint8_t bytes[] = {0x1d, 0x34, 0x12, 0x42, 0x0b};
	const struct zzb_zcl_hdr expected = {
		.type = ZZB_ZCL_FRAME_CLUSTER,
		.flags = ZZB_ZCL_MANUFACTURER | ZZB_ZCL_TO_CLIENT | ZZB_ZCL_DISABLE_DEFAULT_RSP,
		.manufacturer = 0x1234,
		.tsn = 0x42,
		.cmd = 0x0b,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

/* ZCL 2.3.2: reserved bits are ignored on receipt and zero on transmission */
ZTEST(zzb_zcl_frame, test_reserved_bits)
{
	static const uint8_t bytes[] = {0xe1, 0x10, 0x02};
	struct zzb_zcl_hdr hdr;
	uint8_t buf[ZZB_ZCL_HDR_MAX_LEN];

	zassert_equal(zzb_zcl_hdr_parse(bytes, sizeof(bytes), &hdr), 3);
	zassert_equal(hdr.type, ZZB_ZCL_FRAME_CLUSTER);
	zassert_equal(hdr.flags, 0);
	zassert_equal(zzb_zcl_hdr_write(&hdr, buf), 3);
	zassert_equal(buf[0], 0x01);
}

ZTEST(zzb_zcl_frame, test_short_header)
{
	static const uint8_t manufacturer[] = {0x04, 0x34, 0x12, 0x01, 0x00};
	struct zzb_zcl_hdr hdr;

	zassert_equal(zzb_zcl_hdr_parse((const uint8_t[]){0x00, 0x01}, 2, &hdr), -EINVAL);
	zassert_equal(zzb_zcl_hdr_parse(manufacturer, sizeof(manufacturer) - 1, &hdr), -EINVAL);
	zassert_equal(zzb_zcl_hdr_parse(manufacturer, 0, &hdr), -EINVAL);
}

ZTEST(zzb_zcl_frame, test_reserved_frame_type)
{
	struct zzb_zcl_hdr hdr;

	zassert_equal(zzb_zcl_hdr_parse((const uint8_t[]){0x02, 0x01, 0x00}, 3, &hdr), -ENOTSUP);
	zassert_equal(zzb_zcl_hdr_parse((const uint8_t[]){0x03, 0x01, 0x00}, 3, &hdr), -ENOTSUP);
}

/* ZCL Table 2-11 */
ZTEST(zzb_zcl_frame, test_fixed_lengths)
{
	static const struct {
		uint8_t type;
		int len;
	} types[] = {
		{0x00, 0}, {0x08, 1}, {0x0f, 8},  {0x10, 1}, {0x18, 1}, {0x1f, 8}, {0x20, 1},
		{0x22, 3}, {0x27, 8}, {0x28, 1},  {0x2f, 8}, {0x30, 1}, {0x31, 2}, {0x38, 2},
		{0x39, 4}, {0x3a, 8}, {0xe0, 4},  {0xe1, 4}, {0xe2, 4}, {0xe8, 2}, {0xe9, 2},
		{0xea, 4}, {0xf0, 8}, {0xf1, 16}, {0xff, 0},
	};
	static const uint8_t value[16];

	ARRAY_FOR_EACH(types, i) {
		zassert_equal(zzb_zcl_value_len(types[i].type, value, sizeof(value)), types[i].len,
			      "type 0x%02x", types[i].type);
		if (types[i].len > 0) {
			zassert_equal(zzb_zcl_value_len(types[i].type, value, types[i].len - 1),
				      -EINVAL, "type 0x%02x", types[i].type);
		}
	}
}

/* ZCL 2.6.2: a length of 0xff or 0xffff marks an invalid string without characters */
ZTEST(zzb_zcl_frame, test_string_lengths)
{
	zassert_equal(zzb_zcl_value_len(0x42, (const uint8_t[]){0x03, 'a', 'b', 'c'}, 4), 4);
	zassert_equal(zzb_zcl_value_len(0x41, (const uint8_t[]){0x00}, 1), 1);
	zassert_equal(zzb_zcl_value_len(0x42, (const uint8_t[]){0xff}, 1), 1);
	zassert_equal(zzb_zcl_value_len(0x44, (const uint8_t[]){0x02, 0x00, 'a', 'b'}, 4), 4);
	zassert_equal(zzb_zcl_value_len(0x43, (const uint8_t[]){0xff, 0xff}, 2), 2);

	zassert_equal(zzb_zcl_value_len(0x42, (const uint8_t[]){0x03, 'a'}, 2), -EINVAL);
	zassert_equal(zzb_zcl_value_len(0x41, (const uint8_t[]){0x00}, 0), -EINVAL);
	zassert_equal(zzb_zcl_value_len(0x43, (const uint8_t[]){0x00}, 1), -EINVAL);
}

/* Composite types and reserved type IDs */
ZTEST(zzb_zcl_frame, test_unknown_lengths)
{
	static const uint8_t types[] = {0x01, 0x40, 0x48, 0x4c, 0x50, 0x51};
	static const uint8_t value[16];

	ARRAY_FOR_EACH(types, i) {
		zassert_equal(zzb_zcl_value_len(types[i], value, sizeof(value)), -ENOTSUP,
			      "type 0x%02x", types[i]);
	}
}

ZTEST_SUITE(zzb_zcl_frame, NULL, NULL, NULL, NULL, NULL);
