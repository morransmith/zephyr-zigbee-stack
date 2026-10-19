/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "aps/frame.h"
#include "common/buf.h"

static void check_hdr_round_trip(const uint8_t *bytes, size_t len,
				 const struct zzb_aps_hdr *expected)
{
	struct zzb_aps_hdr hdr;
	uint8_t buf[ZZB_BUF_SIZE];

	zassert_equal(zzb_aps_hdr_parse(bytes, len, &hdr), len);
	zassert_equal(hdr.type, expected->type);
	zassert_equal(hdr.delivery, expected->delivery);
	zassert_equal(hdr.flags, expected->flags);
	zassert_equal(hdr.dst_ep, expected->dst_ep);
	zassert_equal(hdr.group, expected->group);
	zassert_equal(hdr.cluster, expected->cluster);
	zassert_equal(hdr.profile, expected->profile);
	zassert_equal(hdr.src_ep, expected->src_ep);
	zassert_equal(hdr.counter, expected->counter);

	zassert_equal(zzb_aps_hdr_len(&hdr), len);
	zassert_equal(zzb_aps_hdr_write(&hdr, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* APS header in the payload of the Ziggurat frame in tests/unit/sec (see NOTICE) */
ZTEST(zzb_aps_frame, test_data_header)
{
	static const uint8_t bytes[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_DATA,
		.delivery = ZZB_APS_DELIVERY_UNICAST,
		.dst_ep = 0x01,
		.cluster = 0x0006,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = 0xa9,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_secured_broadcast)
{
	static const uint8_t bytes[] = {0x28, 0xff, 0x06, 0x00, 0x04, 0x01, 0x01, 0x10};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_DATA,
		.delivery = ZZB_APS_DELIVERY_BROADCAST,
		.flags = ZZB_APS_SECURITY,
		.dst_ep = 0xff,
		.cluster = 0x0006,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = 0x10,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_group_header)
{
	static const uint8_t bytes[] = {0x0c, 0x02, 0x00, 0x06, 0x00, 0x04, 0x01, 0x01, 0x11};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_DATA,
		.delivery = ZZB_APS_DELIVERY_GROUP,
		.group = 0x0002,
		.cluster = 0x0006,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = 0x11,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
	zassert_equal(sizeof(bytes), ZZB_APS_HDR_MAX_LEN);
}

/* APS header of the zigbee-rs Transport Key in tests/unit/sec (see NOTICE) */
ZTEST(zzb_aps_frame, test_command_header)
{
	static const uint8_t bytes[] = {0x21, 0x95};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_CMD,
		.flags = ZZB_APS_SECURITY,
		.counter = 0x95,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

/* R22 2.2.5.2.3: the endpoints are swapped, cluster, profile and counter copied */
ZTEST(zzb_aps_frame, test_data_ack)
{
	static const uint8_t bytes[] = {0x02, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_ACK,
		.dst_ep = 0x01,
		.cluster = 0x0006,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = 0xa9,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_command_ack)
{
	static const uint8_t bytes[] = {0x12, 0x95};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_ACK,
		.flags = ZZB_APS_ACK_FORMAT,
		.counter = 0x95,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_ack_request_and_extended_header)
{
	static const uint8_t bytes[] = {0xc0, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9, 0x00};
	const struct zzb_aps_hdr expected = {
		.type = ZZB_APS_FRAME_DATA,
		.flags = ZZB_APS_ACK_REQ | ZZB_APS_EXT_HDR,
		.dst_ep = 0x01,
		.cluster = 0x0006,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = 0xa9,
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_rejected_headers)
{
	static const uint8_t inter_pan[] = {0x03, 0x06, 0x00, 0x04, 0x01, 0x01};
	static const uint8_t reserved_delivery[] = {0x04, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9};
	/* First fragment of two blocks */
	static const uint8_t fragment[] = {0xc0, 0x01, 0x06, 0x00, 0x04,
					   0x01, 0x01, 0xa9, 0x01, 0x02};
	static const uint8_t reserved_ext[] = {0x80, 0x01, 0x06, 0x00, 0x04,
					       0x01, 0x01, 0xa9, 0x04};
	static const uint8_t data[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9};
	struct zzb_aps_hdr hdr;

	zassert_equal(zzb_aps_hdr_parse(inter_pan, sizeof(inter_pan), &hdr), -ENOTSUP);
	zassert_equal(zzb_aps_hdr_parse(reserved_delivery, sizeof(reserved_delivery), &hdr),
		      -EINVAL);
	zassert_equal(zzb_aps_hdr_parse(fragment, sizeof(fragment), &hdr), -ENOTSUP);
	zassert_equal(zzb_aps_hdr_parse(reserved_ext, sizeof(reserved_ext), &hdr), -EINVAL);
	zassert_equal(zzb_aps_hdr_parse(data, sizeof(data) - 1, &hdr), -EINVAL);
	zassert_equal(zzb_aps_hdr_parse(data, 0, &hdr), -EINVAL);
}

ZTEST_SUITE(zzb_aps_frame, NULL, NULL, NULL, NULL, NULL);
