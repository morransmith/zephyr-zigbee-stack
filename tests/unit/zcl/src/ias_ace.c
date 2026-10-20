/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/ias_ace.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

/* A command with its payload */
struct test_cmd {
	uint8_t id;
	uint8_t len;
	uint8_t payload[32];
};

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the command id with the payload to the server on endpoint 90 and checks the response */
static void command(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[16] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(CIE_EP, ZZB_ZCL_IAS_ACE, frame, 3 + len);
	check_frame(frames, CIE_EP, ZZB_ZCL_IAS_ACE, rsp, sizeof(rsp));
}

/* Sends the server command id with the payload to the client on endpoint 91 and checks the reply */
static void to_client(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x10, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[40] = {0x09, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(KEYPAD_EP, ZZB_ZCL_IAS_ACE, frame, 3 + len);
	check_frame(frames, KEYPAD_EP, ZZB_ZCL_IAS_ACE, rsp, sizeof(rsp));
}

/*
 * The commands reach zcl_cmd_cb as received: Arm All Zones with the code 1234 and zone 0xff,
 * Bypass of zones 1 and 5 without a code, the commands without fields, Get Zone Information, Get
 * Zone Status, and Get Zone Information with an octet after its field, which is ignored
 */
ZTEST(zzb_zcl_ias_ace, test_server_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 7, {0x03, 0x04, '1', '2', '3', '4', 0xff}},
		{0x01, 4, {0x02, 0x01, 0x05, 0x00}},
		{0x02, 0, {0}},
		{0x03, 0, {0}},
		{0x04, 0, {0}},
		{0x05, 0, {0}},
		{0x06, 1, {0x01}},
		{0x07, 0, {0}},
		{0x08, 0, {0}},
		{0x09, 5, {0x00, 0x10, 0x01, 0x03, 0x00}},
		{0x06, 2, {0x01, 0x02}},
	};
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		command(c->id, c->payload, c->len, ZZB_ZCL_SUCCESS);
		zassert_equal(zcl_cmd_calls.count, 1);
		zassert_equal(cmd->ep, CIE_EP);
		zassert_equal(cmd->cluster, ZZB_ZCL_IAS_ACE);
		zassert_equal(cmd->id, c->id);
		zassert_true(cmd->server);
		zassert_equal(cmd->len, c->len);
		zassert_mem_equal(zcl_cmd_calls.payload[0], c->payload, c->len);
	}
}

/*
 * ZCL 8.3.2.3.1.5: the application answers Arm with zzb_zcl_cmd_respond(), here with command 0x42
 * of the fakes in place of Arm Response, and no Default Response follows
 */
ZTEST(zzb_zcl_ias_ace, test_arm_response)
{
	static const uint8_t arm[] = {0x01, 0x30, 0x00, 0x00, 0x04, '1', '2', '3', '4', 0x00};
	static const uint8_t rsp[] = {0x19, 0x30, 0x42, 0xaa};

	zcl_cmd_responds = true;
	receive_ep(CIE_EP, ZZB_ZCL_IAS_ACE, arm, sizeof(arm));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, CIE_EP, ZZB_ZCL_IAS_ACE, rsp, sizeof(rsp));
}

/* Emergency, which has no response, gets the Default Response with the status of zcl_cmd_cb */
ZTEST(zzb_zcl_ias_ace, test_emergency_status)
{
	zcl_cmd_status = ZZB_ZCL_FAILURE;
	command(ZZB_ZCL_IAS_ACE_EMERGENCY, NULL, 0, ZZB_ZCL_FAILURE);
	zassert_equal(zcl_cmd_calls.count, 1);
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: Arm without fields, without the zone
 * ID and with a code shorter than its length, Bypass without fields, with fewer zones than its
 * count, without the length of the code and with a code shorter than its length, Get Zone
 * Information without its field and Get Zone Status with 4 octets. An unknown command gives
 * UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_ias_ace, test_server_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
		{0x00, 2, {0x03, 0x00}},
		{0x00, 5, {0x03, 0x04, '1', '2', 0xff}},
		{0x01, 0, {0}},
		{0x01, 2, {0x02, 0x01}},
		{0x01, 3, {0x02, 0x01, 0x05}},
		{0x01, 4, {0x01, 0x01, 0x02, '1'}},
		{0x06, 0, {0}},
		{0x09, 4, {0x00, 0x10, 0x01, 0x03}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		command(c->id, c->payload, c->len, ZZB_ZCL_MALFORMED_COMMAND);
		zassert_equal(zcl_cmd_calls.count, 0);
	}

	command(0x0a, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 0);
}

/* Panel Status Changed goes from the server on endpoint 90 to the bound clients */
ZTEST(zzb_zcl_ias_ace, test_panel_status_changed)
{
	static const uint8_t payload[] = {0x04, 0x1e, 0x01, 0x00};
	uint8_t next_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_send_bound(test_node(), CIE_EP, ZZB_ZCL_IAS_ACE,
				      ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED, payload,
				      sizeof(payload)));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(sent[0].req.src_ep, CIE_EP);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_IAS_ACE);
	zassert_equal(sent[0].len, 7);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x19, next_tsn, 0x04, 0x04, 0x1e, 0x01, 0x00}),
			  7);
}

/*
 * The commands of the server reach zcl_cmd_cb of the client as received: Arm Response with All
 * Zones Armed, Get Zone ID Map Response with zones 0 and 1, Get Zone Information Response of zone 1
 * with the IEEE address 0x0807060504030201 and the label Door, Zone Status Changed of zone 1
 * without a label, Panel Status Changed with an exit delay of 30 s, Get Panel Status Response, Set
 * Bypassed Zone List of zones 1 and 5, Bypass Response of two zones and Get Zone Status Response of
 * zones 1 and 5
 */
ZTEST(zzb_zcl_ias_ace, test_client_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 1, {0x03}},
		{0x01, 32, {0x03}},
		{0x02, 16, {0x01, 0x15, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0x04, 'D', 'o', 'o', 'r'}},
		{0x03, 5, {0x01, 0x01, 0x00, 0x01, 0x00}},
		{0x04, 4, {0x04, 0x1e, 0x01, 0x00}},
		{0x05, 4, {0x00, 0x00, 0x01, 0x00}},
		{0x06, 3, {0x02, 0x01, 0x05}},
		{0x07, 3, {0x02, 0x00, 0x04}},
		{0x08, 8, {0x01, 0x02, 0x01, 0x01, 0x00, 0x05, 0x00, 0x00}},
	};
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_client(c->id, c->payload, c->len, ZZB_ZCL_SUCCESS);
		zassert_equal(zcl_cmd_calls.count, 1);
		zassert_equal(cmd->ep, KEYPAD_EP);
		zassert_equal(cmd->id, c->id);
		zassert_false(cmd->server);
		zassert_equal(cmd->len, c->len);
		zassert_mem_equal(zcl_cmd_calls.payload[0], c->payload, c->len);
	}
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: Arm Response without its field, Get
 * Zone ID Map Response with 31 octets, Get Zone Information Response without the label and with a
 * label shorter than its length, Zone Status Changed in the same way, Panel Status Changed and Get
 * Panel Status Response with 3 octets, Set Bypassed Zone List without its count and with fewer
 * zones, Bypass Response with fewer results, Get Zone Status Response without its count and with
 * fewer zones. An unknown command gives UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_ias_ace, test_client_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
		{0x01, 31, {0x03}},
		{0x02, 11, {0x01, 0x15, 0x00, 1, 2, 3, 4, 5, 6, 7, 8}},
		{0x02, 14, {0x01, 0x15, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0x04, 'D', 'o'}},
		{0x03, 4, {0x01, 0x01, 0x00, 0x01}},
		{0x03, 6, {0x01, 0x01, 0x00, 0x01, 0x02, 'D'}},
		{0x04, 3, {0x04, 0x1e, 0x01}},
		{0x05, 3, {0x00, 0x00, 0x01}},
		{0x06, 0, {0}},
		{0x06, 2, {0x02, 0x01}},
		{0x07, 2, {0x02, 0x00}},
		{0x08, 1, {0x01}},
		{0x08, 6, {0x01, 0x02, 0x01, 0x01, 0x00, 0x05}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_client(c->id, c->payload, c->len, ZZB_ZCL_MALFORMED_COMMAND);
		zassert_equal(zcl_cmd_calls.count, 0);
	}

	to_client(0x09, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 0);
}

/* ClusterRevision of the server and the client is 1 (ZCL 8.3.1.1) */
ZTEST(zzb_zcl_ias_ace, test_cluster_revisions)
{
	static const uint8_t server_rsp[] = {0x18, 0x31, 0x01, 0xfd, 0xff, 0x00, 0x21, 0x01, 0x00};
	static const uint8_t client_rsp[] = {0x10, 0x32, 0x01, 0xfd, 0xff, 0x00, 0x21, 0x01, 0x00};

	receive_ep(CIE_EP, ZZB_ZCL_IAS_ACE, (uint8_t[]){0x00, 0x31, 0x00, 0xfd, 0xff}, 5);
	check_frame(0, CIE_EP, ZZB_ZCL_IAS_ACE, server_rsp, sizeof(server_rsp));
	receive_ep(KEYPAD_EP, ZZB_ZCL_IAS_ACE, (uint8_t[]){0x08, 0x32, 0x00, 0xfd, 0xff}, 5);
	check_frame(1, KEYPAD_EP, ZZB_ZCL_IAS_ACE, client_rsp, sizeof(client_rsp));
}

ZTEST_SUITE(zzb_zcl_ias_ace, NULL, setup, before, after, NULL);
