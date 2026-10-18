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

/* Parses bytes, writes the result back and returns it for field checks */
static struct zzb_nwk_cmd check_cmd_round_trip(const uint8_t *bytes, size_t len)
{
	struct zzb_nwk_cmd cmd;
	uint8_t buf[ZZB_BUF_SIZE];

	zassert_equal(zzb_nwk_cmd_parse(bytes, len, &cmd), len);
	zassert_equal(zzb_nwk_cmd_write(&cmd, buf), len);
	zassert_mem_equal(buf, bytes, len);

	return cmd;
}

#define EPID 0x0102030405060708ULL

static const uint8_t route_req[] = {0x01, 0x08, 0x2a, 0xfc, 0xff, 0x00};
static const uint8_t route_req_ieee[] = {
	0x01, 0x20, 0x2b, 0x34, 0x12, 0x03, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
};

ZTEST(zzb_nwk_frame, test_route_request)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(route_req, sizeof(route_req));

	zassert_equal(cmd.id, ZZB_NWK_CMD_ROUTE_REQ);
	zassert_equal(FIELD_GET(ZZB_NWK_ROUTE_REQ_MANY_TO_ONE, cmd.route_req.options), 1);
	zassert_equal(cmd.route_req.id, 42);
	zassert_equal(cmd.route_req.dst, 0xfffc);
	zassert_equal(cmd.route_req.path_cost, 0);

	cmd = check_cmd_round_trip(route_req_ieee, sizeof(route_req_ieee));
	zassert_equal(cmd.route_req.options, ZZB_NWK_ROUTE_REQ_DST_IEEE);
	zassert_equal(cmd.route_req.dst, 0x1234);
	zassert_equal(cmd.route_req.path_cost, 3);
	zassert_equal(cmd.route_req.dst_ext, EPID);
}

static const uint8_t route_reply[] = {
	0x02, 0x30, 0x2b, 0x00, 0x00, 0x34, 0x12, 0x05, 0x77, 0x66, 0x55, 0x44,
	0x33, 0x22, 0x11, 0x00, 0xa8, 0xef, 0x17, 0x1e, 0x00, 0x4b, 0x12, 0x00,
};

ZTEST(zzb_nwk_frame, test_route_reply)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(route_reply, sizeof(route_reply));

	zassert_equal(cmd.id, ZZB_NWK_CMD_ROUTE_REPLY);
	zassert_equal(cmd.route_reply.options,
		      ZZB_NWK_ROUTE_REPLY_ORIG_IEEE | ZZB_NWK_ROUTE_REPLY_RESP_IEEE);
	zassert_equal(cmd.route_reply.id, 43);
	zassert_equal(cmd.route_reply.orig, 0x0000);
	zassert_equal(cmd.route_reply.resp, 0x1234);
	zassert_equal(cmd.route_reply.path_cost, 5);
	zassert_equal(cmd.route_reply.orig_ext, 0x0011223344556677ULL);
	zassert_equal(cmd.route_reply.resp_ext, 0x00124b001e17efa8ULL);
}

static const uint8_t nwk_status[] = {0x03, 0x0d, 0x34, 0x12};

/* Address conflict with the conflicting address, then PAN identifier update without address */
ZTEST(zzb_nwk_frame, test_network_status)
{
	static const uint8_t no_addr[] = {0x03, 0x0f};
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(nwk_status, sizeof(nwk_status));

	zassert_equal(cmd.id, ZZB_NWK_CMD_NWK_STATUS);
	zassert_equal(cmd.nwk_status.code, 0x0d);
	zassert_true(cmd.nwk_status.has_addr);
	zassert_equal(cmd.nwk_status.addr, 0x1234);

	cmd = check_cmd_round_trip(no_addr, sizeof(no_addr));
	zassert_equal(cmd.nwk_status.code, 0x0f);
	zassert_false(cmd.nwk_status.has_addr);
}

static const uint8_t leave[] = {0x04, 0x60};

ZTEST(zzb_nwk_frame, test_leave)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(leave, sizeof(leave));

	zassert_equal(cmd.id, ZZB_NWK_CMD_LEAVE);
	zassert_equal(cmd.leave_options, ZZB_NWK_LEAVE_REJOIN | ZZB_NWK_LEAVE_REQUEST);
}

static const uint8_t route_record[] = {0x05, 0x02, 0x11, 0x11, 0x22, 0x22};

ZTEST(zzb_nwk_frame, test_route_record)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(route_record, sizeof(route_record));

	zassert_equal(cmd.id, ZZB_NWK_CMD_ROUTE_RECORD);
	zassert_equal(cmd.route_record.count, 2);
	zassert_equal(cmd.route_record.relays, route_record + 2);
}

/* test_rejoin_request_round_trip and test_rejoin_response_round_trip in Ziggurat (see NOTICE) */
static const uint8_t rejoin_req[] = {0x06, 0x8e};
static const uint8_t rejoin_rsp[] = {0x07, 0x34, 0x12, 0x00};

ZTEST(zzb_nwk_frame, test_rejoin)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(rejoin_req, sizeof(rejoin_req));

	zassert_equal(cmd.id, ZZB_NWK_CMD_REJOIN_REQ);
	zassert_equal(cmd.capability, 0x8e);

	cmd = check_cmd_round_trip(rejoin_rsp, sizeof(rejoin_rsp));
	zassert_equal(cmd.id, ZZB_NWK_CMD_REJOIN_RSP);
	zassert_equal(cmd.rejoin_rsp.short_addr, 0x1234);
	zassert_equal(cmd.rejoin_rsp.status, 0x00);
}

static const uint8_t link_status[] = {0x08, 0x62, 0x01, 0x00, 0x11, 0x34, 0x12, 0x73};

ZTEST(zzb_nwk_frame, test_link_status)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(link_status, sizeof(link_status));

	zassert_equal(cmd.id, ZZB_NWK_CMD_LINK_STATUS);
	zassert_equal(cmd.link_status.flags,
		      ZZB_NWK_LINK_STATUS_FIRST_FRAME | ZZB_NWK_LINK_STATUS_LAST_FRAME);
	zassert_equal(cmd.link_status.count, 2);
	zassert_equal(cmd.link_status.entries, link_status + 2);
}

static const uint8_t nwk_report[] = {
	0x09, 0x02, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x62, 0x1a, 0x78, 0x56,
};

ZTEST(zzb_nwk_frame, test_network_report)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(nwk_report, sizeof(nwk_report));

	zassert_equal(cmd.id, ZZB_NWK_CMD_NWK_REPORT);
	zassert_equal(cmd.nwk_report.epid, EPID);
	zassert_equal(cmd.nwk_report.count, 2);
	zassert_equal(cmd.nwk_report.pan_ids, nwk_report + 10);
}

static const uint8_t nwk_update[] = {
	0x0a, 0x01, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x05, 0x63, 0x1a,
};

ZTEST(zzb_nwk_frame, test_network_update)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(nwk_update, sizeof(nwk_update));

	zassert_equal(cmd.id, ZZB_NWK_CMD_NWK_UPDATE);
	zassert_equal(cmd.nwk_update.epid, EPID);
	zassert_equal(cmd.nwk_update.update_id, 5);
	zassert_equal(cmd.nwk_update.pan_id, 0x1a63);
}

static const uint8_t ed_timeout_req[] = {0x0b, 0x08, 0x00};
static const uint8_t ed_timeout_rsp[] = {0x0c, 0x00, 0x03};

ZTEST(zzb_nwk_frame, test_end_device_timeout)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(ed_timeout_req, sizeof(ed_timeout_req));

	zassert_equal(cmd.id, ZZB_NWK_CMD_ED_TIMEOUT_REQ);
	zassert_equal(cmd.ed_timeout_req.timeout, 8);
	zassert_equal(cmd.ed_timeout_req.config, 0);

	cmd = check_cmd_round_trip(ed_timeout_rsp, sizeof(ed_timeout_rsp));
	zassert_equal(cmd.id, ZZB_NWK_CMD_ED_TIMEOUT_RSP);
	zassert_equal(cmd.ed_timeout_rsp.status, 0);
	zassert_equal(cmd.ed_timeout_rsp.parent_info, 0x03);
}

static const uint8_t power_delta[] = {0x0d, 0x00, 0x01, 0x00, 0x00, 0xf6};

ZTEST(zzb_nwk_frame, test_link_power_delta)
{
	struct zzb_nwk_cmd cmd = check_cmd_round_trip(power_delta, sizeof(power_delta));

	zassert_equal(cmd.id, ZZB_NWK_CMD_LINK_POWER_DELTA);
	zassert_equal(cmd.power_delta.type, 0);
	zassert_equal(cmd.power_delta.count, 1);
	zassert_equal(cmd.power_delta.entries, power_delta + 3);
}

ZTEST(zzb_nwk_frame, test_command_trailing_bytes_are_ignored)
{
	static const uint8_t bytes[] = {0x06, 0x8e, 0x01};
	struct zzb_nwk_cmd cmd;

	zassert_equal(zzb_nwk_cmd_parse(bytes, sizeof(bytes), &cmd), 2);
}

ZTEST(zzb_nwk_frame, test_command_rejected)
{
	static const uint8_t reserved_ids[] = {0x00, 0x0e};
	static const uint8_t report_type_1[] = {
		0x09, 0x22, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x62, 0x1a, 0x78, 0x56,
	};
	static const uint8_t update_type_1[] = {
		0x0a, 0x21, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x05, 0x63, 0x1a,
	};
	static const uint8_t update_two_pan_ids[] = {
		0x0a, 0x02, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
		0x02, 0x01, 0x05, 0x63, 0x1a, 0x64, 0x1a,
	};
	struct zzb_nwk_cmd cmd;

	ARRAY_FOR_EACH(reserved_ids, i) {
		zassert_equal(zzb_nwk_cmd_parse(&reserved_ids[i], 1, &cmd), -ENOTSUP);
	}

	zassert_equal(zzb_nwk_cmd_parse(report_type_1, sizeof(report_type_1), &cmd), -ENOTSUP);
	zassert_equal(zzb_nwk_cmd_parse(update_type_1, sizeof(update_type_1), &cmd), -ENOTSUP);
	zassert_equal(zzb_nwk_cmd_parse(update_two_pan_ids, sizeof(update_two_pan_ids), &cmd),
		      -EINVAL);
}

ZTEST(zzb_nwk_frame, test_command_truncated)
{
	/* The network status address is optional, so its truncation is not tested here */
	static const struct {
		const uint8_t *bytes;
		size_t len;
	} cmds[] = {
		{route_req, sizeof(route_req)},           {route_req_ieee, sizeof(route_req_ieee)},
		{route_reply, sizeof(route_reply)},       {leave, sizeof(leave)},
		{route_record, sizeof(route_record)},     {rejoin_req, sizeof(rejoin_req)},
		{rejoin_rsp, sizeof(rejoin_rsp)},         {link_status, sizeof(link_status)},
		{nwk_report, sizeof(nwk_report)},         {nwk_update, sizeof(nwk_update)},
		{ed_timeout_req, sizeof(ed_timeout_req)}, {ed_timeout_rsp, sizeof(ed_timeout_rsp)},
		{power_delta, sizeof(power_delta)},
	};
	struct zzb_nwk_cmd cmd;

	zassert_equal(zzb_nwk_cmd_parse(nwk_status, 1, &cmd), -EINVAL);

	ARRAY_FOR_EACH(cmds, i) {
		zassert_equal(zzb_nwk_cmd_parse(cmds[i].bytes, cmds[i].len - 1, &cmd), -EINVAL,
			      "command 0x%02x", cmds[i].bytes[0]);
	}
}

static void check_beacon_round_trip(const uint8_t *bytes, const struct zzb_nwk_beacon *expected)
{
	struct zzb_nwk_beacon beacon;
	uint8_t buf[ZZB_NWK_BEACON_LEN];

	zassert_equal(zzb_nwk_beacon_parse(bytes, ZZB_NWK_BEACON_LEN, &beacon), ZZB_NWK_BEACON_LEN);
	zassert_equal(beacon.stack_profile, expected->stack_profile);
	zassert_equal(beacon.protocol_version, expected->protocol_version);
	zassert_equal(beacon.router_capacity, expected->router_capacity);
	zassert_equal(beacon.depth, expected->depth);
	zassert_equal(beacon.ed_capacity, expected->ed_capacity);
	zassert_equal(beacon.ext_pan_id, expected->ext_pan_id);
	zassert_equal(beacon.tx_offset, expected->tx_offset);
	zassert_equal(beacon.update_id, expected->update_id);

	zassert_equal(zzb_nwk_beacon_write(&beacon, buf), ZZB_NWK_BEACON_LEN);
	zassert_mem_equal(buf, bytes, ZZB_NWK_BEACON_LEN);
}

/* test_zigbee_beacon_round_trip in Ziggurat (see NOTICE) */
static const uint8_t coord_beacon[] = {
	0x00, 0x22, 0x84, 0x93, 0xcb, 0x3c, 0x0b, 0x01, 0x44, 0x9f, 0x3a, 0xff, 0xff, 0xff, 0x00,
};

ZTEST(zzb_nwk_frame, test_coordinator_beacon_payload)
{
	const struct zzb_nwk_beacon expected = {
		.stack_profile = 2,
		.protocol_version = 2,
		.router_capacity = true,
		.depth = 0,
		.ed_capacity = true,
		.ext_pan_id = 0x3a9f44010b3ccb93ULL,
		.tx_offset = 0xffffff,
		.update_id = 0,
	};

	check_beacon_round_trip(coord_beacon, &expected);
}

ZTEST(zzb_nwk_frame, test_router_beacon_payload)
{
	static const uint8_t bytes[] = {
		0x00, 0x22, 0x2c, 0x08, 0x07, 0x06, 0x05, 0x04,
		0x03, 0x02, 0x01, 0x56, 0x34, 0x12, 0x07,
	};
	const struct zzb_nwk_beacon expected = {
		.stack_profile = 2,
		.protocol_version = 2,
		.router_capacity = true,
		.depth = 5,
		.ext_pan_id = EPID,
		.tx_offset = 0x123456,
		.update_id = 7,
	};

	check_beacon_round_trip(bytes, &expected);
}

ZTEST(zzb_nwk_frame, test_beacon_payload_rejected)
{
	uint8_t other_protocol[ZZB_NWK_BEACON_LEN];
	struct zzb_nwk_beacon beacon;

	memcpy(other_protocol, coord_beacon, sizeof(other_protocol));
	other_protocol[0] = 0x01;

	zassert_equal(zzb_nwk_beacon_parse(other_protocol, ZZB_NWK_BEACON_LEN, &beacon), -ENOTSUP);
	zassert_equal(zzb_nwk_beacon_parse(coord_beacon, ZZB_NWK_BEACON_LEN - 1, &beacon), -EINVAL);
}

ZTEST_SUITE(zzb_nwk_frame, NULL, NULL, NULL, NULL, NULL);
