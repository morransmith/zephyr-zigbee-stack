/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "mac/frame.h"

static void check_addr(const struct zzb_mac_addr *addr, const struct zzb_mac_addr *expected)
{
	zassert_equal(addr->mode, expected->mode);
	zassert_equal(addr->pan_id, expected->pan_id);
	zassert_equal(addr->short_addr, expected->short_addr);
	zassert_equal(addr->ext_addr, expected->ext_addr);
}

static void check_hdr_round_trip(const uint8_t *bytes, size_t len,
				 const struct zzb_mac_hdr *expected)
{
	struct zzb_mac_hdr hdr;
	uint8_t buf[ZZB_MAC_HDR_MAX_LEN];

	zassert_equal(zzb_mac_hdr_parse(bytes, len, &hdr), len);
	zassert_equal(hdr.type, expected->type);
	zassert_equal(hdr.flags, expected->flags);
	zassert_equal(hdr.seq, expected->seq);
	check_addr(&hdr.dst, &expected->dst);
	check_addr(&hdr.src, &expected->src);

	zassert_equal(zzb_mac_hdr_len(&hdr), len);
	zassert_equal(zzb_mac_hdr_write(&hdr, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* MAC header of test_frame_data in Ziggurat (see NOTICE) */
static const uint8_t data_hdr[] = {0x61, 0x88, 0xa3, 0xf5, 0x3e, 0x34, 0x52, 0x63, 0xf6};

ZTEST(zzb_mac_frame, test_data_header)
{
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_DATA,
		.flags = ZZB_FRAME_ACK_REQ,
		.seq = 163,
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0x3ef5, .short_addr = 0x5234},
		.src = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0x3ef5, .short_addr = 0xf663},
	};

	check_hdr_round_trip(data_hdr, sizeof(data_hdr), &expected);
}

/* test_frame_ack in Ziggurat (see NOTICE), then the same ACK with frame pending set */
ZTEST(zzb_mac_frame, test_ack_header)
{
	static const uint8_t ack[] = {0x02, 0x00, 0xd1};
	static const uint8_t ack_pending[] = {0x12, 0x00, 0xd1};
	struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_ACK,
		.seq = 209,
	};

	check_hdr_round_trip(ack, sizeof(ack), &expected);

	expected.flags = ZZB_FRAME_PENDING;
	check_hdr_round_trip(ack_pending, sizeof(ack_pending), &expected);
}

/* MAC header of test_beacon_request in Ziggurat (see NOTICE) */
ZTEST(zzb_mac_frame, test_beacon_request_header)
{
	static const uint8_t bytes[] = {0x03, 0x08, 0x6d, 0xff, 0xff, 0xff, 0xff};
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.seq = 109,
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0xffff, .short_addr = 0xffff},
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

/* Source address of the Ziggurat association request */
static const uint64_t joiner_addr = 0xbc026efffe494a31ULL;

/* MAC header of test_association_request in Ziggurat (see NOTICE), R22 Table D-1 first option */
static const uint8_t assoc_req_hdr[] = {
	0x23, 0xc8, 0x13, 0x26, 0x1c, 0x00, 0x00, 0xff, 0xff,
	0x31, 0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc,
};

ZTEST(zzb_mac_frame, test_association_request_header)
{
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.flags = ZZB_FRAME_ACK_REQ,
		.seq = 19,
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0x1c26, .short_addr = 0x0000},
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0xffff, .ext_addr = joiner_addr},
	};

	check_hdr_round_trip(assoc_req_hdr, sizeof(assoc_req_hdr), &expected);
}

/* R22 Table D-1 other options: IntraPAN set, then no destination (sent to the PAN coordinator) */
ZTEST(zzb_mac_frame, test_association_request_header_options)
{
	static const uint8_t intra_pan[] = {
		0x63, 0xc8, 0x13, 0x26, 0x1c, 0x00, 0x00, 0x31,
		0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc,
	};
	static const uint8_t no_dst[] = {
		0x23, 0xc0, 0x13, 0x26, 0x1c, 0x31, 0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc,
	};
	struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.flags = ZZB_FRAME_ACK_REQ,
		.seq = 19,
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0x1c26, .short_addr = 0x0000},
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0x1c26, .ext_addr = joiner_addr},
	};

	check_hdr_round_trip(intra_pan, sizeof(intra_pan), &expected);

	expected.dst = (struct zzb_mac_addr){.mode = ZZB_MAC_ADDR_NONE};
	check_hdr_round_trip(no_dst, sizeof(no_dst), &expected);
}

ZTEST(zzb_mac_frame, test_truncated_header_is_rejected)
{
	struct zzb_mac_hdr hdr;

	zassert_equal(zzb_mac_hdr_parse(assoc_req_hdr, sizeof(assoc_req_hdr) - 1, &hdr), -EINVAL);
	zassert_equal(zzb_mac_hdr_parse(assoc_req_hdr, 2, &hdr), -EINVAL);
}

ZTEST(zzb_mac_frame, test_reserved_values_are_rejected)
{
	/* Frame type 4, then destination addressing mode 1 */
	static const uint8_t type[] = {0x04, 0x00, 0x00};
	static const uint8_t mode[] = {0x01, 0x04, 0x00, 0xff, 0xff, 0xff, 0xff};
	struct zzb_mac_hdr hdr;

	zassert_equal(zzb_mac_hdr_parse(type, sizeof(type), &hdr), -EINVAL);
	zassert_equal(zzb_mac_hdr_parse(mode, sizeof(mode), &hdr), -EINVAL);
}

/* The data header with frame version 1, frame version 2, then with Security Enabled set */
ZTEST(zzb_mac_frame, test_frame_version_and_security)
{
	uint8_t bytes[sizeof(data_hdr)];
	struct zzb_mac_hdr hdr;

	memcpy(bytes, data_hdr, sizeof(bytes));
	bytes[1] = 0x98;
	zassert_equal(zzb_mac_hdr_parse(bytes, sizeof(bytes), &hdr), sizeof(bytes));

	bytes[1] = 0xa8;
	zassert_equal(zzb_mac_hdr_parse(bytes, sizeof(bytes), &hdr), -ENOTSUP);

	bytes[0] = 0x69;
	bytes[1] = 0x88;
	zassert_equal(zzb_mac_hdr_parse(bytes, sizeof(bytes), &hdr), -ENOTSUP);
}

ZTEST_SUITE(zzb_mac_frame, NULL, NULL, NULL, NULL, NULL);
