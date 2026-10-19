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

/* Every field of the command type is written, so equal bytes mean equal fields */
static void check_cmd_round_trip(const uint8_t *bytes, size_t len,
				 const struct zzb_aps_cmd *expected)
{
	struct zzb_aps_cmd cmd;
	uint8_t buf[ZZB_BUF_SIZE];

	zassert_equal(zzb_aps_cmd_write(expected, buf), len);
	zassert_mem_equal(buf, bytes, len);

	zassert_equal(zzb_aps_cmd_parse(bytes, len, &cmd), len);
	zassert_equal(cmd.id, expected->id);
	zassert_equal(zzb_aps_cmd_write(&cmd, buf), len);
	zassert_mem_equal(buf, bytes, len);
}

/* Payload of the zigbee-rs Transport Key in tests/unit/sec (see NOTICE) */
ZTEST(zzb_aps_frame, test_transport_key_with_network_key)
{
	static const uint8_t bytes[] = {
		0x05, 0x01, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe5, 0x01, 0x30, 0x38, 0x9c,
		0x38, 0xc1, 0xa4, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce, 0xf4,
	};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_NWK,
		.transport_key.key = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89},
		.transport_key.seq = 0,
		.transport_key.dst = 0xa4c1389c383001e5ULL,
		.transport_key.src = 0xf4ce36c17d3852e1ULL,
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_transport_key_with_trust_center_link_key)
{
	static const uint8_t bytes[] = {
		0x05, 0x04, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
		0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xe5, 0x01, 0x30, 0x38, 0x9c, 0x38,
		0xc1, 0xa4, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce, 0xf4,
	};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_TC_LINK,
		.transport_key.key = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
				      0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f},
		.transport_key.dst = 0xa4c1389c383001e5ULL,
		.transport_key.src = 0xf4ce36c17d3852e1ULL,
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_transport_key_with_application_link_key)
{
	static const uint8_t bytes[] = {
		0x05, 0x03, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
		0x1c, 0x1d, 0x1e, 0x1f, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x01,
	};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_APP_LINK,
		.transport_key.key = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
				      0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f},
		.transport_key.partner = 0x0011223344556677ULL,
		.transport_key.initiator = true,
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_request_key)
{
	static const uint8_t tc_link[] = {0x08, 0x04};
	static const uint8_t app_link[] = {0x08, 0x02, 0x77, 0x66, 0x55,
					   0x44, 0x33, 0x22, 0x11, 0x00};
	const struct zzb_aps_cmd tc_link_cmd = {
		.id = ZZB_APS_CMD_REQUEST_KEY,
		.request_key = {.type = ZZB_APS_REQUEST_TC_LINK},
	};
	const struct zzb_aps_cmd app_link_cmd = {
		.id = ZZB_APS_CMD_REQUEST_KEY,
		.request_key.type = ZZB_APS_REQUEST_APP_LINK,
		.request_key.partner = 0x0011223344556677ULL,
	};

	check_cmd_round_trip(tc_link, sizeof(tc_link), &tc_link_cmd);
	check_cmd_round_trip(app_link, sizeof(app_link), &app_link_cmd);
}

ZTEST(zzb_aps_frame, test_switch_key)
{
	static const uint8_t bytes[] = {0x09, 0x01};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_SWITCH_KEY,
		.switch_key_seq = 1,
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_verify_key)
{
	static const uint8_t bytes[] = {
		0x0f, 0x04, 0xe5, 0x01, 0x30, 0x38, 0x9c, 0x38, 0xc1, 0xa4, 0x20, 0x21, 0x22,
		0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_VERIFY_KEY,
		.verify_key.type = ZZB_APS_KEY_TC_LINK,
		.verify_key.src = 0xa4c1389c383001e5ULL,
		.verify_key.hash = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
				    0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f},
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_confirm_key)
{
	static const uint8_t bytes[] = {0x10, 0x00, 0x04, 0xe5, 0x01, 0x30,
					0x38, 0x9c, 0x38, 0xc1, 0xa4};
	const struct zzb_aps_cmd expected = {
		.id = ZZB_APS_CMD_CONFIRM_KEY,
		.confirm_key.status = 0x00,
		.confirm_key.type = ZZB_APS_KEY_TC_LINK,
		.confirm_key.dst = 0xa4c1389c383001e5ULL,
	};

	check_cmd_round_trip(bytes, sizeof(bytes), &expected);
}

ZTEST(zzb_aps_frame, test_rejected_commands)
{
	/* Update Device belongs to the router */
	static const uint8_t update_device[] = {0x06, 0x77, 0x66, 0x55, 0x44, 0x33,
						0x22, 0x11, 0x00, 0x34, 0x12, 0x00};
	static const uint8_t reserved_key_type[] = {0x05, 0x02};
	static const uint8_t reserved_request_type[] = {0x08, 0x01};
	static const uint8_t confirm_key[] = {0x10, 0x00, 0x04, 0xe5, 0x01, 0x30,
					      0x38, 0x9c, 0x38, 0xc1, 0xa4};
	struct zzb_aps_cmd cmd;

	zassert_equal(zzb_aps_cmd_parse(update_device, sizeof(update_device), &cmd), -ENOTSUP);
	zassert_equal(zzb_aps_cmd_parse(reserved_key_type, sizeof(reserved_key_type), &cmd),
		      -ENOTSUP);
	zassert_equal(zzb_aps_cmd_parse(reserved_request_type, sizeof(reserved_request_type), &cmd),
		      -ENOTSUP);
	zassert_equal(zzb_aps_cmd_parse(confirm_key, sizeof(confirm_key) - 1, &cmd), -EINVAL);
	zassert_equal(zzb_aps_cmd_parse(confirm_key, 0, &cmd), -EINVAL);
}

ZTEST_SUITE(zzb_aps_frame, NULL, NULL, NULL, NULL, NULL);
