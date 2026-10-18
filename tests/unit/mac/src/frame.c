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

/*
 * Beacon of a coordinator that permits association: MAC header, Superframe Specification, empty
 * GTS and pending address fields, and the beacon payload of test_zigbee_beacon_round_trip in
 * Ziggurat (see NOTICE).
 */
static const uint8_t beacon[] = {
	0x00, 0x80, 0x42, 0x26, 0x1c, 0x00, 0x00, 0xff, 0xcf, 0x00, 0x00, 0x00, 0x22,
	0x84, 0x93, 0xcb, 0x3c, 0x0b, 0x01, 0x44, 0x9f, 0x3a, 0xff, 0xff, 0xff, 0x00,
};

ZTEST(zzb_mac_frame, test_coordinator_beacon)
{
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_BEACON,
		.seq = 0x42,
		.src = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0x1c26, .short_addr = 0x0000},
	};
	uint16_t superframe;
	uint8_t buf[4];

	check_hdr_round_trip(beacon, 7, &expected);

	zassert_equal(zzb_mac_beacon_parse(beacon + 7, sizeof(beacon) - 7, &superframe), 4);
	zassert_equal(superframe, 0x0fff | ZZB_MAC_SF_PAN_COORD | ZZB_MAC_SF_ASSOC_PERMIT);

	zassert_equal(zzb_mac_beacon_write(superframe, buf), 4);
	zassert_mem_equal(buf, beacon + 7, 4);
}

/* MAC payload with one GTS descriptor, one short and one extended pending address */
static const uint8_t gts_beacon[] = {
	0xff, 0xcf, 0x81, 0x01, 0x34, 0x12, 0x1e, 0x11, 0x78, 0x56,
	0x31, 0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc, 0x00, 0x22,
};

ZTEST(zzb_mac_frame, test_beacon_gts_and_pending_fields_are_skipped)
{
	uint16_t superframe;

	zassert_equal(zzb_mac_beacon_parse(gts_beacon, sizeof(gts_beacon), &superframe), 18);
	zassert_equal(superframe, 0xcfff);
	zassert_equal(zzb_mac_beacon_parse(gts_beacon, 18, &superframe), 18);
}

ZTEST(zzb_mac_frame, test_truncated_beacon_is_rejected)
{
	uint16_t superframe;

	zassert_equal(zzb_mac_beacon_parse(beacon + 7, 3, &superframe), -EINVAL);
	zassert_equal(zzb_mac_beacon_parse(gts_beacon, 6, &superframe), -EINVAL);
	zassert_equal(zzb_mac_beacon_parse(gts_beacon, 17, &superframe), -EINVAL);
}

/* Parses a command, checks that writing it back gives the same bytes and returns it */
static struct zzb_mac_cmd cmd_round_trip(const uint8_t *bytes, size_t len)
{
	struct zzb_mac_cmd cmd;
	uint8_t buf[8];

	zassert_equal(zzb_mac_cmd_parse(bytes, len, &cmd), len);
	zassert_equal(zzb_mac_cmd_write(&cmd, buf), len);
	zassert_mem_equal(buf, bytes, len);

	return cmd;
}

/* Command of test_association_request in Ziggurat (see NOTICE) */
ZTEST(zzb_mac_frame, test_association_request_command)
{
	static const uint8_t bytes[] = {0x01, 0x8e};
	struct zzb_mac_cmd cmd = cmd_round_trip(bytes, sizeof(bytes));

	zassert_equal(cmd.id, ZZB_MAC_CMD_ASSOC_REQ);
	zassert_equal(cmd.capability, 0x8e);
}

/* Extended address of the coordinator in the frames below */
static const uint64_t coord_addr = 0x00124b00012e5a7fULL;

/* R22 Table D-3 first option */
ZTEST(zzb_mac_frame, test_association_response)
{
	static const uint8_t frame[] = {
		0x63, 0xcc, 0x14, 0x26, 0x1c, 0x31, 0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc,
		0x7f, 0x5a, 0x2e, 0x01, 0x00, 0x4b, 0x12, 0x00, 0x02, 0x3a, 0x5f, 0x00,
	};
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.flags = ZZB_FRAME_ACK_REQ,
		.seq = 0x14,
		.dst = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0x1c26, .ext_addr = joiner_addr},
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0x1c26, .ext_addr = coord_addr},
	};
	struct zzb_mac_cmd cmd;

	check_hdr_round_trip(frame, 21, &expected);

	cmd = cmd_round_trip(frame + 21, sizeof(frame) - 21);
	zassert_equal(cmd.id, ZZB_MAC_CMD_ASSOC_RSP);
	zassert_equal(cmd.assoc_rsp.short_addr, 0x5f3a);
	zassert_equal(cmd.assoc_rsp.status, 0x00);
}

/* Data request, orphan notification, and beacon request of test_beacon_request in Ziggurat */
ZTEST(zzb_mac_frame, test_commands_without_payload)
{
	static const uint8_t data_req[] = {0x04};
	static const uint8_t orphan[] = {0x06};
	static const uint8_t beacon_req[] = {0x07};

	zassert_equal(cmd_round_trip(data_req, 1).id, ZZB_MAC_CMD_DATA_REQ);
	zassert_equal(cmd_round_trip(orphan, 1).id, ZZB_MAC_CMD_ORPHAN);
	zassert_equal(cmd_round_trip(beacon_req, 1).id, ZZB_MAC_CMD_BEACON_REQ);
}

/* 802.15.4 5.3.6: broadcast PAN ID and short address, PAN ID compression, 15-octet MHR */
ZTEST(zzb_mac_frame, test_orphan_notification_header)
{
	static const uint8_t bytes[] = {
		0x43, 0xc8, 0x15, 0xff, 0xff, 0xff, 0xff, 0x31,
		0x4a, 0x49, 0xfe, 0xff, 0x6e, 0x02, 0xbc,
	};
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.seq = 0x15,
		.dst = {.mode = ZZB_MAC_ADDR_SHORT, .pan_id = 0xffff, .short_addr = 0xffff},
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0xffff, .ext_addr = joiner_addr},
	};

	check_hdr_round_trip(bytes, sizeof(bytes), &expected);
}

/* Coordinator realignment sent to an orphaned device (802.15.4 5.3.8) */
static const uint8_t realign[] = {
	0x23, 0xcc, 0x16, 0xff, 0xff, 0x31, 0x4a, 0x49, 0xfe, 0xff, 0x6e,
	0x02, 0xbc, 0x26, 0x1c, 0x7f, 0x5a, 0x2e, 0x01, 0x00, 0x4b, 0x12,
	0x00, 0x08, 0x26, 0x1c, 0x00, 0x00, 0x0f, 0x3a, 0x5f,
};

static void check_realign_cmd(const struct zzb_mac_cmd *cmd)
{
	zassert_equal(cmd->id, ZZB_MAC_CMD_COORD_REALIGN);
	zassert_equal(cmd->realign.pan_id, 0x1c26);
	zassert_equal(cmd->realign.coord_short_addr, 0x0000);
	zassert_equal(cmd->realign.channel, 15);
	zassert_equal(cmd->realign.short_addr, 0x5f3a);
}

ZTEST(zzb_mac_frame, test_coordinator_realignment)
{
	const struct zzb_mac_hdr expected = {
		.type = ZZB_MAC_FRAME_CMD,
		.flags = ZZB_FRAME_ACK_REQ,
		.seq = 0x16,
		.dst = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0xffff, .ext_addr = joiner_addr},
		.src = {.mode = ZZB_MAC_ADDR_EXT, .pan_id = 0x1c26, .ext_addr = coord_addr},
	};
	struct zzb_mac_cmd cmd;

	check_hdr_round_trip(realign, ZZB_MAC_HDR_MAX_LEN, &expected);

	cmd = cmd_round_trip(realign + ZZB_MAC_HDR_MAX_LEN, sizeof(realign) - ZZB_MAC_HDR_MAX_LEN);
	check_realign_cmd(&cmd);
}

ZTEST(zzb_mac_frame, test_coordinator_realignment_channel_page_is_ignored)
{
	uint8_t bytes[9];
	struct zzb_mac_cmd cmd;

	memcpy(bytes, realign + ZZB_MAC_HDR_MAX_LEN, 8);
	bytes[8] = 0x00;

	zassert_equal(zzb_mac_cmd_parse(bytes, sizeof(bytes), &cmd), 8);
	check_realign_cmd(&cmd);
}

/* Disassociation notification, PAN ID conflict notification, GTS request */
ZTEST(zzb_mac_frame, test_unsupported_commands_are_rejected)
{
	static const uint8_t disassoc[] = {0x03, 0x02};
	static const uint8_t pan_id_conflict[] = {0x05};
	static const uint8_t gts_req[] = {0x09, 0x00};
	struct zzb_mac_cmd cmd;

	zassert_equal(zzb_mac_cmd_parse(disassoc, sizeof(disassoc), &cmd), -ENOTSUP);
	zassert_equal(zzb_mac_cmd_parse(pan_id_conflict, sizeof(pan_id_conflict), &cmd), -ENOTSUP);
	zassert_equal(zzb_mac_cmd_parse(gts_req, sizeof(gts_req), &cmd), -ENOTSUP);
}

ZTEST(zzb_mac_frame, test_truncated_command_is_rejected)
{
	static const uint8_t assoc_rsp[] = {0x02, 0x3a, 0x5f};
	struct zzb_mac_cmd cmd;

	zassert_equal(zzb_mac_cmd_parse(assoc_rsp, sizeof(assoc_rsp), &cmd), -EINVAL);
	zassert_equal(zzb_mac_cmd_parse(realign + ZZB_MAC_HDR_MAX_LEN, 7, &cmd), -EINVAL);
	zassert_equal(zzb_mac_cmd_parse(assoc_rsp, 0, &cmd), -EINVAL);
}

ZTEST_SUITE(zzb_mac_frame, NULL, NULL, NULL, NULL, NULL);
