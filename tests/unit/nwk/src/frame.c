/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "nwk/frame.h"

static void check_hdr_round_trip(const uint8_t *bytes, size_t len,
				 const struct zzb_nwk_hdr *expected)
{
	struct zzb_nwk_hdr hdr;
	uint8_t buf[ZZB_BUF_SIZE];

	zassert_equal(zzb_nwk_hdr_parse(bytes, len, &hdr), len);
	zassert_equal(hdr.type, expected->type);
	zassert_equal(hdr.flags, expected->flags);
	zassert_equal(hdr.dst, expected->dst);
	zassert_equal(hdr.src, expected->src);
	zassert_equal(hdr.radius, expected->radius);
	zassert_equal(hdr.seq, expected->seq);
	zassert_equal(hdr.dst_ext, expected->dst_ext);
	zassert_equal(hdr.src_ext, expected->src_ext);
	zassert_equal(hdr.mcast_ctrl, expected->mcast_ctrl);
	zassert_equal(hdr.relay_count, expected->relay_count);
	zassert_equal(hdr.relay_index, expected->relay_index);
	zassert_mem_equal(hdr.relays, expected->relays, sizeof(hdr.relays));

	zassert_equal(zzb_nwk_hdr_len(&hdr), len);
	zassert_equal(zzb_nwk_hdr_write(&hdr, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* NWK header of the Ziggurat frame in tests/unit/sec (see NOTICE) */
static const uint8_t data_hdr[] = {0x08, 0x02, 0x42, 0x6b, 0x00, 0x00, 0x0f, 0x2e};

ZTEST(zzb_nwk_frame, test_data_header)
{
	const struct zzb_nwk_hdr expected = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY,
		.dst = 0x6b42,
		.src = 0x0000,
		.radius = 15,
		.seq = 0x2e,
	};

	check_hdr_round_trip(data_hdr, sizeof(data_hdr), &expected);
}

ZTEST(zzb_nwk_frame, test_ieee_addresses)
{
	static const uint8_t bytes[] = {
		0x09, 0x1a, 0x00, 0x00, 0x34, 0x12, 0x01, 0x55, 0x77, 0x66, 0x55, 0x44,
		0x33, 0x22, 0x11, 0x00, 0xa8, 0xef, 0x17, 0x1e, 0x00, 0x4b, 0x12, 0x00,
	};
	const struct zzb_nwk_hdr expected = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_DST_IEEE | ZZB_NWK_SRC_IEEE,
		.dst = 0x0000,
		.src = 0x1234,
		.radius = 1,
		.seq = 0x55,
		.dst_ext = 0x0011223344556677ULL,
		.src_ext = 0x00124b001e17efa8ULL,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_nwk_frame, test_multicast_control)
{
	/* Member mode, NonmemberRadius 3, MaxNonmemberRadius 7 */
	static const uint8_t bytes[] = {0x08, 0x01, 0x01, 0x00, 0x34, 0x12, 0x1e, 0x10, 0xed};
	const struct zzb_nwk_hdr expected = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_MULTICAST,
		.dst = 0x0001,
		.src = 0x1234,
		.radius = 30,
		.seq = 0x10,
		.mcast_ctrl = 0xed,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

static const uint8_t src_route_hdr[] = {
	0x08, 0x06, 0x34, 0x12, 0x00, 0x00, 0x1e, 0x20, 0x02, 0x01, 0x11, 0x11, 0x22, 0x22,
};

ZTEST(zzb_nwk_frame, test_source_route)
{
	const struct zzb_nwk_hdr expected = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_ROUTE,
		.dst = 0x1234,
		.src = 0x0000,
		.radius = 30,
		.seq = 0x20,
		.relay_count = 2,
		.relay_index = 1,
		.relays = {0x1111, 0x2222},
	};

	check_hdr_round_trip(src_route_hdr, sizeof(src_route_hdr), &expected);
}

ZTEST(zzb_nwk_frame, test_discover_route_and_end_device_initiator)
{
	static const uint8_t bytes[] = {0x48, 0x22, 0x00, 0x00, 0x34, 0x12, 0x1e, 0x30};
	const struct zzb_nwk_hdr expected = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_DISCOVER_ROUTE | ZZB_NWK_SECURITY | ZZB_NWK_ED_INITIATOR,
		.dst = 0x0000,
		.src = 0x1234,
		.radius = 30,
		.seq = 0x30,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_nwk_frame, test_header_rejected)
{
	static const uint8_t version_1[] = {0x04, 0x00, 0x00, 0x00, 0x34, 0x12, 0x1e, 0x30};
	static const uint8_t inter_pan[] = {0x0b, 0x00};
	static const uint8_t reserved_type[] = {0x0a, 0x00, 0x00, 0x00, 0x34, 0x12, 0x1e, 0x30};
	uint8_t too_many_relays[8 + 2 + 2 * (ZZB_NWK_MAX_RELAYS + 1)] = {
		0x08, 0x04, 0x34, 0x12, 0x00, 0x00, 0x1e, 0x20, ZZB_NWK_MAX_RELAYS + 1,
	};
	struct zzb_nwk_hdr hdr;

	zassert_equal(zzb_nwk_hdr_parse(version_1, sizeof(version_1), &hdr), -ENOTSUP);
	zassert_equal(zzb_nwk_hdr_parse(inter_pan, sizeof(inter_pan), &hdr), -ENOTSUP);
	zassert_equal(zzb_nwk_hdr_parse(reserved_type, sizeof(reserved_type), &hdr), -EINVAL);
	zassert_equal(zzb_nwk_hdr_parse(too_many_relays, sizeof(too_many_relays), &hdr), -ENOTSUP);
}

ZTEST(zzb_nwk_frame, test_header_truncated)
{
	struct zzb_nwk_hdr hdr;

	zassert_equal(zzb_nwk_hdr_parse(data_hdr, 1, &hdr), -EINVAL);
	zassert_equal(zzb_nwk_hdr_parse(data_hdr, sizeof(data_hdr) - 1, &hdr), -EINVAL);
	/* Relay count present, relay list one byte short */
	zassert_equal(zzb_nwk_hdr_parse(src_route_hdr, sizeof(src_route_hdr) - 1, &hdr), -EINVAL);
}

ZTEST_SUITE(zzb_nwk_frame, NULL, NULL, NULL, NULL, NULL);
