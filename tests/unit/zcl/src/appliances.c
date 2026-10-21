/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/appliance_control.h>
#include <zzb/zcl/appliance_events.h>
#include <zzb/zcl/appliance_stats.h>

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

/* Sends c of cluster to the server on endpoint 140 and checks the Default Response */
static void to_server(uint16_t cluster, const struct test_cmd *c, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, c->id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[40] = {0x01, tsn, c->id};

	memcpy(frame + 3, c->payload, c->len);
	receive_ep(WHITE_GOODS_EP, cluster, frame, 3 + c->len);
	check_frame(frames, WHITE_GOODS_EP, cluster, rsp, sizeof(rsp));
}

/* Sends the server command c of cluster to the client on endpoint 141 and checks the reply */
static void to_client(uint16_t cluster, const struct test_cmd *c, uint8_t status)
{
	const uint8_t rsp[] = {0x10, ++tsn, 0x0b, c->id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[40] = {0x09, tsn, c->id};

	memcpy(frame + 3, c->payload, c->len);
	receive_ep(GATEWAY_EP, cluster, frame, 3 + c->len);
	check_frame(frames, GATEWAY_EP, cluster, rsp, sizeof(rsp));
}

/*
 * Sends each of the count commands of cluster to the server or the client, and checks that it
 * reaches zcl_cmd_cb as received
 */
static void check_passed(uint16_t cluster, bool server, const struct test_cmd *commands,
			 size_t count)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	for (const struct test_cmd *c = commands; c < commands + count; c++) {
		reset_fakes();
		if (server) {
			to_server(cluster, c, ZZB_ZCL_SUCCESS);
		} else {
			to_client(cluster, c, ZZB_ZCL_SUCCESS);
		}

		zassert_equal(zcl_cmd_calls.count, 1);
		zassert_equal(cmd->ep, server ? WHITE_GOODS_EP : GATEWAY_EP);
		zassert_equal(cmd->cluster, cluster);
		zassert_equal(cmd->id, c->id);
		zassert_equal(cmd->server, server);
		zassert_equal(cmd->len, c->len);
		zassert_mem_equal(zcl_cmd_calls.payload[0], c->payload, c->len);
	}
}

/*
 * Sends each of the count commands of cluster to the server or the client, and checks that it gets
 * status without reaching zcl_cmd_cb
 */
static void check_refused(uint16_t cluster, bool server, const struct test_cmd *commands,
			  size_t count, uint8_t status)
{
	for (const struct test_cmd *c = commands; c < commands + count; c++) {
		reset_fakes();
		if (server) {
			to_server(cluster, c, status);
		} else {
			to_client(cluster, c, status);
		}

		zassert_equal(zcl_cmd_calls.count, 0);
	}
}

/*
 * The commands of ZCL 15.2.4 reach zcl_cmd_cb of the Appliance Control server as received:
 * Execution of a Command with START, Signal State, Write Functions of StartTime 14:30 absolute
 * (uint16) and of a function 0x0100 with the character string "eco", Write Functions without
 * records, Overload Pause Resume, Overload Pause, and Overload Warning with its enum8 and with the
 * 2 octets of ZCL Figure 15-5
 */
ZTEST(zzb_zcl_appliances, test_control_server_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 1, {0x01}},
		{0x01, 0, {0}},
		{0x02, 12, {0x00, 0x00, 0x21, 0x5e, 0x0e, 0x00, 0x01, 0x42, 0x03, 'e', 'c', 'o'}},
		{0x02, 0, {0}},
		{0x03, 0, {0}},
		{0x04, 0, {0}},
		{0x05, 1, {0x01}},
		{0x05, 2, {0x01, 0x00}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_CONTROL, true, commands, ARRAY_SIZE(commands));
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: Execution of a Command and Overload
 * Warning without their field, and Write Functions with a record without its data type, with 1 of
 * the 2 octets of a uint16, with a string shorter than its length, and with an array, whose length
 * the stack does not know. An unknown command gives UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_appliances, test_control_server_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
		{0x05, 0, {0}},
		{0x02, 2, {0x00, 0x00}},
		{0x02, 4, {0x00, 0x00, 0x21, 0x5e}},
		{0x02, 10, {0x00, 0x00, 0x21, 0x5e, 0x0e, 0x00, 0x01, 0x42, 0x03, 'e'}},
		{0x02, 6, {0x00, 0x01, 0x48, 0x20, 0x00, 0x00}},
	};

	check_refused(ZZB_ZCL_APPLIANCE_CONTROL, true, commands, ARRAY_SIZE(commands),
		      ZZB_ZCL_MALFORMED_COMMAND);
	check_refused(ZZB_ZCL_APPLIANCE_CONTROL, true, &(struct test_cmd){0x06}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

/*
 * Signal State Response of a running appliance with remote control enabled, and Signal State
 * Notification of a paused appliance with Appliance Status 2 as IRIS symptom code, reach zcl_cmd_cb
 * of the client as received. Signal State Response with one octet and Signal State Notification
 * without fields give MALFORMED_COMMAND, an unknown command UNSUP_COMMAND.
 */
ZTEST(zzb_zcl_appliances, test_control_client_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 2, {0x05, 0x0f}},
		{0x01, 5, {0x06, 0x2f, 0x01, 0x02, 0x03}},
	};
	static const struct test_cmd malformed[] = {
		{0x00, 1, {0x05}},
		{0x01, 0, {0}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_CONTROL, false, commands, ARRAY_SIZE(commands));
	check_refused(ZZB_ZCL_APPLIANCE_CONTROL, false, malformed, ARRAY_SIZE(malformed),
		      ZZB_ZCL_MALFORMED_COMMAND);
	check_refused(ZZB_ZCL_APPLIANCE_CONTROL, false, &(struct test_cmd){0x02}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

/* Execution of a Command with STOP goes from the client on endpoint 141 to the bound servers */
ZTEST(zzb_zcl_appliances, test_execution_of_command)
{
	uint8_t node_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_appliance_control_execution_of_command(test_node(), GATEWAY_EP,
								  ZZB_ZCL_APPLIANCE_STOP));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.src_ep, GATEWAY_EP);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_APPLIANCE_CONTROL);
	zassert_equal(sent[0].len, 4);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x11, node_tsn, 0x00, 0x02}), 4);
}

/* The server reads Command Identification of Execution of a Command */
ZTEST(zzb_zcl_appliances, test_execution_of_command_parse)
{
	const struct zzb_zcl_cmd cmd = {.payload = (uint8_t[]){0x03}, .len = 1};
	struct zzb_zcl_appliance_control_execution_of_command fields;

	zzb_zcl_appliance_control_execution_of_command_parse(&cmd, &fields);
	zassert_equal(fields.command, ZZB_ZCL_APPLIANCE_PAUSE);
}

static int answer_signal_state(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_zcl_appliance_control_signal_state_rsp(
		node, cmd, ZZB_ZCL_APPLIANCE_RUNNING, ZZB_ZCL_APPLIANCE_REMOTE_CONTROL,
		ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_PROPRIETARY_1);
}

/*
 * ZCL 15.2.4.2.1: Signal State gets Signal State Response of a running appliance with remote
 * control enabled and Device Status 2 0x1, without Appliance Status 2
 */
ZTEST(zzb_zcl_appliances, test_signal_state_rsp)
{
	static const uint8_t req[] = {0x01, 0x30, 0x01};
	static const uint8_t rsp[] = {0x19, 0x30, 0x00, 0x05, 0x1f};

	zcl_cmd_answer = answer_signal_state;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_CONTROL, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_CONTROL, rsp, sizeof(rsp));
}

/*
 * The client reads Signal State Response of a paused appliance with remote control enabled and
 * Appliance Status 2 as IRIS symptom code
 */
ZTEST(zzb_zcl_appliances, test_signal_state_rsp_parse)
{
	const struct zzb_zcl_cmd cmd = {.payload = (uint8_t[]){0x06, 0x2f, 0x01, 0x02, 0x03},
					.len = 5};
	struct zzb_zcl_appliance_control_signal_state_rsp fields;

	zzb_zcl_appliance_control_signal_state_rsp_parse(&cmd, &fields);
	zassert_equal(fields.appliance_status, ZZB_ZCL_APPLIANCE_PAUSED);
	zassert_equal(fields.remote, ZZB_ZCL_APPLIANCE_REMOTE_CONTROL);
	zassert_equal(fields.device_status2, ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_IRIS_SYMPTOM_CODE);
	zassert_equal(fields.appliance_status2, 0x030201);
}

/*
 * ZCL 15.2.4.1.2: Signal State Notification of a paused appliance with remote control temporarily
 * locked goes from the server on endpoint 140 to the bound clients
 */
ZTEST(zzb_zcl_appliances, test_signal_state_notification)
{
	uint8_t node_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_appliance_control_signal_state_notification(
		test_node(), WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_PAUSED,
		ZZB_ZCL_APPLIANCE_REMOTE_TEMPORARILY_LOCKED,
		ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_PROPRIETARY_0));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.src_ep, WHITE_GOODS_EP);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_APPLIANCE_CONTROL);
	zassert_equal(sent[0].len, 5);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x19, node_tsn, 0x01, 0x06, 0x07}), 5);
}

/*
 * The client reads Signal State Notification of a running appliance with remote and energy
 * control enabled; Appliance Status 2, which the payload lacks, is 0
 */
ZTEST(zzb_zcl_appliances, test_signal_state_notification_parse)
{
	const struct zzb_zcl_cmd cmd = {.payload = (uint8_t[]){0x05, 0x01}, .len = 2};
	struct zzb_zcl_appliance_control_signal_state_rsp fields = {.appliance_status2 = 1};

	zzb_zcl_appliance_control_signal_state_notification_parse(&cmd, &fields);
	zassert_equal(fields.appliance_status, ZZB_ZCL_APPLIANCE_RUNNING);
	zassert_equal(fields.remote, ZZB_ZCL_APPLIANCE_REMOTE_AND_ENERGY_CONTROL);
	zassert_equal(fields.device_status2, ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_PROPRIETARY_0);
	zassert_equal(fields.appliance_status2, 0);
}

/*
 * Get Alerts reaches zcl_cmd_cb of the Appliance Events and Alerts server, an unknown command gets
 * UNSUP_COMMAND
 */
ZTEST(zzb_zcl_appliances, test_events_server_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_EVENTS, true, commands, ARRAY_SIZE(commands));
	check_refused(ZZB_ZCL_APPLIANCE_EVENTS, true, &(struct test_cmd){0x01}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

/*
 * The commands of ZCL 15.4.2.4 reach zcl_cmd_cb of the client as received: Get Alerts Response of
 * a present warning and a recovered failure, whose Alerts Count has bits 4 to 7 set, which do not
 * count, Alerts Notification of one alert and of none, and Event Notification of the end of the
 * cycle
 */
ZTEST(zzb_zcl_appliances, test_events_client_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 7, {0x12, 0x01, 0x11, 0x00, 0x02, 0x03, 0x00}},
		{0x01, 4, {0x01, 0x01, 0x11, 0x00}},
		{0x01, 1, {0x00}},
		{0x02, 2, {0x00, 0x01}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_EVENTS, false, commands, ARRAY_SIZE(commands));
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: Get Alerts Response without Alerts
 * Count and with 2 of the 3 octets of its second alert, Alerts Notification with 2 octets of its
 * alert and Event Notification with one octet. An unknown command gives UNSUP_COMMAND. None
 * reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_appliances, test_events_client_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
		{0x00, 6, {0x02, 0x01, 0x11, 0x00, 0x02, 0x03}},
		{0x01, 3, {0x01, 0x01, 0x11}},
		{0x02, 1, {0x00}},
	};

	check_refused(ZZB_ZCL_APPLIANCE_EVENTS, false, commands, ARRAY_SIZE(commands),
		      ZZB_ZCL_MALFORMED_COMMAND);
	check_refused(ZZB_ZCL_APPLIANCE_EVENTS, false, &(struct test_cmd){0x03}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

/* The number of alerts or log IDs that the answers of the tests send */
static size_t answer_count;

/* A present warning with ID 0x11 and a recovered failure with ID 0x02 and manufacturer bits 0x80 */
static const uint32_t alerts[ZZB_ZCL_APPLIANCE_ALERTS_MAX + 1] = {
	FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_ID, 0x11) |
		FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_CATEGORY, ZZB_ZCL_APPLIANCE_ALERT_WARNING) |
		FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_PRESENCE_RECOVERY,
			   ZZB_ZCL_APPLIANCE_ALERT_PRESENCE),
	FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_ID, 0x02) |
		FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_CATEGORY, ZZB_ZCL_APPLIANCE_ALERT_FAILURE) |
		FIELD_PREP(ZZB_ZCL_APPLIANCE_ALERT_MANUFACTURER, 0x80),
};

static int answer_alerts(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_zcl_appliance_events_get_alerts_rsp(node, cmd, alerts, answer_count);
}

/*
 * ZCL 15.4.2.3.1.2: Get Alerts gets Get Alerts Response with two unstructured alerts. The 15
 * alerts that Alerts Count holds go out, 16 give -EMSGSIZE.
 */
ZTEST(zzb_zcl_appliances, test_get_alerts_rsp)
{
	static const uint8_t req[] = {0x01, 0x30, 0x00};
	static const uint8_t rsp[] = {0x19, 0x30, 0x00, 0x02, 0x11, 0x11, 0x00, 0x02, 0x03, 0x80};

	zcl_cmd_answer = answer_alerts;
	answer_count = 2;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_EVENTS, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_EVENTS, rsp, sizeof(rsp));

	answer_count = ZZB_ZCL_APPLIANCE_ALERTS_MAX;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_EVENTS, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(sent[1].data[3], 0x0f);

	answer_count = ZZB_ZCL_APPLIANCE_ALERTS_MAX + 1;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_EVENTS, req, sizeof(req));
	zassert_equal(zcl_cmd_respond_err, -EMSGSIZE);
}

/*
 * Log Request of log 5 and Log Queue Request reach zcl_cmd_cb of the Appliance Statistics server
 * as received. Log Request with 3 octets gives MALFORMED_COMMAND, an unknown command
 * UNSUP_COMMAND.
 */
ZTEST(zzb_zcl_appliances, test_stats_server_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 4, {0x05, 0x00, 0x00, 0x00}},
		{0x01, 0, {0}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_STATS, true, commands, ARRAY_SIZE(commands));
	check_refused(ZZB_ZCL_APPLIANCE_STATS, true, &(struct test_cmd){0x00, 3, {0x05}}, 1,
		      ZZB_ZCL_MALFORMED_COMMAND);
	check_refused(ZZB_ZCL_APPLIANCE_STATS, true, &(struct test_cmd){0x02}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

static const uint32_t log_ids[ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX + 1] = {5, 0x01020304};

static int answer_log_queue(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_zcl_appliance_stats_log_queue_rsp(node, cmd, log_ids, answer_count);
}

/*
 * ZCL 15.5.2.2.3: Log Queue Request gets Log Queue Response with logs 5 and 0x01020304. The most
 * log IDs that a frame holds go out, one more gives -EMSGSIZE.
 */
ZTEST(zzb_zcl_appliances, test_log_queue_rsp)
{
	static const uint8_t req[] = {0x01, 0x30, 0x01};
	static const uint8_t rsp[] = {0x19, 0x30, 0x02, 0x02, 0x05, 0x00,
				      0x00, 0x00, 0x04, 0x03, 0x02, 0x01};

	zcl_cmd_answer = answer_log_queue;
	answer_count = 2;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_STATS, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_STATS, rsp, sizeof(rsp));

	answer_count = ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_STATS, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);

	answer_count = ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX + 1;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_APPLIANCE_STATS, req, sizeof(req));
	zassert_equal(zcl_cmd_respond_err, -EMSGSIZE);
}

/*
 * The commands of ZCL 15.5.2.2 reach zcl_cmd_cb of the client as received: Log Notification of log
 * 5 without a time stamp and with 3 octets of log payload, Log Response of log 5 with the time
 * stamp 0x30001000 and an empty log, Log Queue Response of logs 5 and 6, and Statistics Available
 * of no log
 */
ZTEST(zzb_zcl_appliances, test_stats_client_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 15, {0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x03}},
		{0x01, 12, {0x00, 0x10, 0x00, 0x30, 0x05}},
		{0x02, 9, {0x02, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00}},
		{0x03, 1, {0x00}},
	};

	check_passed(ZZB_ZCL_APPLIANCE_STATS, false, commands, ARRAY_SIZE(commands));
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: Log Notification with 11 octets, with
 * 2 of the 3 octets of its log payload and with Log Length 0xffffffff, Log Response without
 * fields, Log Queue Response with 3 octets of its second log ID and Statistics Available without
 * its count. An unknown command gives UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_appliances, test_stats_client_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 11, {0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00}},
		{0x00, 14, {0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x03}},
		{0x00,
		 13,
		 {0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x01}},
		{0x01, 0, {0}},
		{0x02, 8, {0x02, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00}},
		{0x03, 0, {0}},
	};

	check_refused(ZZB_ZCL_APPLIANCE_STATS, false, commands, ARRAY_SIZE(commands),
		      ZZB_ZCL_MALFORMED_COMMAND);
	check_refused(ZZB_ZCL_APPLIANCE_STATS, false, &(struct test_cmd){0x04}, 1,
		      ZZB_ZCL_UNSUP_COMMAND);
}

ZTEST_SUITE(zzb_zcl_appliances, NULL, setup, before, after, NULL);
