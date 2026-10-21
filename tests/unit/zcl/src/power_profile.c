/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/power_profile.h>

#include "fakes.h"

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

/* Each test starts with the defaults, without records */
static void before(void *fixture)
{
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the command id with the payload to the server on endpoint 140, checks the response */
static void to_server(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[40] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, frame, 3 + len);
	check_frame(frames, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, rsp, sizeof(rsp));
}

/* Sends the server command id with the payload to the client on endpoint 141, checks the reply */
static void to_client(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x10, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[40] = {0x09, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(GATEWAY_EP, ZZB_ZCL_POWER_PROFILE, frame, 3 + len);
	check_frame(frames, GATEWAY_EP, ZZB_ZCL_POWER_PROFILE, rsp, sizeof(rsp));
}

/* Checks that zcl_cmd_cb received c, and no other command, on ep */
static void check_passed(uint8_t ep, bool server, const struct test_cmd *c)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	zassert_equal(zcl_cmd_calls.count, 1);
	zassert_equal(cmd->ep, ep);
	zassert_equal(cmd->cluster, ZZB_ZCL_POWER_PROFILE);
	zassert_equal(cmd->id, c->id);
	zassert_equal(cmd->server, server);
	zassert_equal(cmd->len, c->len);
	zassert_mem_equal(zcl_cmd_calls.payload[0], c->payload, c->len);
}

/*
 * The commands of ZCL 3.17.5 reach zcl_cmd_cb of the server as received: PowerProfileRequest of
 * profile 1, PowerProfileStateRequest, GetPowerProfilePriceResponse of 1.15 EUR,
 * GetOverallSchedulePriceResponse, EnergyPhasesScheduleNotification of two phases and of none,
 * EnergyPhasesScheduleResponse of one phase, PowerProfileScheduleConstraintsRequest,
 * EnergyPhasesScheduleStateRequest, GetPowerProfilePriceExtendedResponse, and PowerProfileRequest
 * with an octet after its field, which is ignored
 */
ZTEST(zzb_zcl_power_profile, test_server_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 1, {0x01}},
		{0x01, 0, {0}},
		{0x02, 8, {0x01, 0xd2, 0x03, 0x73, 0x00, 0x00, 0x00, 0x02}},
		{0x03, 7, {0xd2, 0x03, 0x73, 0x00, 0x00, 0x00, 0x02}},
		{0x04, 8, {0x01, 0x02, 0x01, 0x1e, 0x00, 0x02, 0x0a, 0x00}},
		{0x04, 2, {0x01, 0x00}},
		{0x05, 5, {0x01, 0x01, 0x01, 0x3c, 0x00}},
		{0x06, 1, {0x01}},
		{0x07, 1, {0x01}},
		{0x08, 8, {0x01, 0xd2, 0x03, 0x73, 0x00, 0x00, 0x00, 0x02}},
		{0x00, 2, {0x01, 0x02}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_server(c->id, c->payload, c->len, ZZB_ZCL_SUCCESS);
		check_passed(WHITE_GOODS_EP, true, c);
	}
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: PowerProfileRequest without its
 * field, the price responses with an octet less, EnergyPhasesScheduleNotification without the
 * number of phases and with fewer phases than that number, EnergyPhasesScheduleResponse,
 * PowerProfileScheduleConstraintsRequest and EnergyPhasesScheduleStateRequest without fields. An
 * unknown command gives UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_power_profile, test_server_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 0, {0}},
		{0x02, 7, {0x01, 0xd2, 0x03, 0x73, 0x00, 0x00, 0x00}},
		{0x03, 6, {0xd2, 0x03, 0x73, 0x00, 0x00, 0x00}},
		{0x04, 1, {0x01}},
		{0x04, 7, {0x01, 0x02, 0x01, 0x1e, 0x00, 0x02, 0x0a}},
		{0x05, 0, {0}},
		{0x06, 0, {0}},
		{0x07, 0, {0}},
		{0x08, 7, {0x01, 0xd2, 0x03, 0x73, 0x00, 0x00, 0x00}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_server(c->id, c->payload, c->len, ZZB_ZCL_MALFORMED_COMMAND);
		zassert_equal(zcl_cmd_calls.count, 0);
	}

	to_server(0x09, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 0);
}

/*
 * ZCL 3.17.5.1.3: the application answers PowerProfileRequest with zzb_zcl_cmd_respond(), here
 * with command 0x42 of the fakes in place of PowerProfileResponse, and no Default Response follows
 */
ZTEST(zzb_zcl_power_profile, test_response)
{
	static const uint8_t req[] = {0x01, 0x30, 0x00, 0x01};
	static const uint8_t rsp[] = {0x19, 0x30, 0x42, 0xaa};

	zcl_cmd_responds = true;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, rsp, sizeof(rsp));
}

/* ZCL 3.17.5.5.3: a refused schedule gets the Default Response with the status of zcl_cmd_cb */
ZTEST(zzb_zcl_power_profile, test_schedule_refused)
{
	static const uint8_t schedule[] = {0x01, 0x01, 0x01, 0x00, 0x00};

	zcl_cmd_status = ZZB_ZCL_NOT_AUTHORIZED;
	to_server(ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_NOTIFICATION, schedule,
		  sizeof(schedule), ZZB_ZCL_NOT_AUTHORIZED);
	zassert_equal(zcl_cmd_calls.count, 1);
}

/* The server reads PowerProfileID of PowerProfileRequest, here of the request for every profile */
ZTEST(zzb_zcl_power_profile, test_request_parse)
{
	const struct zzb_zcl_cmd cmd = {.payload = (uint8_t[]){0x00}, .len = 1};
	struct zzb_zcl_power_profile_request fields = {.power_profile_id = 1};

	zzb_zcl_power_profile_request_parse(&cmd, &fields);
	zassert_equal(fields.power_profile_id, ZZB_ZCL_POWER_PROFILE_ID_ALL);
}

/* The number of phases or records that the answers of the tests send */
static size_t answer_count;

/*
 * Two energy phases: 120 minutes with 2000 W and 50.0 Wh of energy, then 30 minutes with 100 W
 * and 5.0 Wh after a pause of at most 10 minutes
 */
static const struct zzb_zcl_power_profile_phase phases[ZZB_ZCL_POWER_PROFILE_PHASES_MAX + 1] = {
	{1, 1, 120, 2000, 500, ZZB_ZCL_POWER_PROFILE_ACTIVATION_DELAY_FIRST},
	{2, 3, 30, 100, 50, 10},
};

static int answer_profile(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_zcl_power_profile_rsp(node, cmd, 2, 1, phases, answer_count);
}

/*
 * ZCL 3.17.5.1.3: PowerProfileRequest of profile 1 gets PowerProfileResponse of profile 1 of 2
 * with two energy phases. The most phases that a frame holds go out, one more gives -EMSGSIZE.
 */
ZTEST(zzb_zcl_power_profile, test_power_profile_rsp)
{
	static const uint8_t req[] = {0x01, 0x30, 0x00, 0x01};
	static const uint8_t rsp[] = {0x19, 0x30, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x78,
				      0x00, 0xd0, 0x07, 0xf4, 0x01, 0xff, 0xff, 0x02, 0x03,
				      0x1e, 0x00, 0x64, 0x00, 0x32, 0x00, 0x0a, 0x00};

	zcl_cmd_answer = answer_profile;
	answer_count = 2;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, rsp, sizeof(rsp));

	answer_count = ZZB_ZCL_POWER_PROFILE_PHASES_MAX;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);

	answer_count = ZZB_ZCL_POWER_PROFILE_PHASES_MAX + 1;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_equal(zcl_cmd_respond_err, -EMSGSIZE);
}

/* A running profile that is remotely controllable and an idle one */
static const struct zzb_zcl_power_profile_record records[ZZB_ZCL_POWER_PROFILE_RECORDS_MAX + 1] = {
	{1, 1, true, ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_RUNNING},
	{2, ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_INVALID, false, ZZB_ZCL_POWER_PROFILE_IDLE},
};

static int answer_state(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_zcl_power_profile_state_rsp(node, cmd, records, answer_count);
}

/*
 * ZCL 3.17.5.2.1: PowerProfileStateRequest gets PowerProfileStateResponse with two records. The
 * most records that a frame holds go out, one more gives -EMSGSIZE.
 */
ZTEST(zzb_zcl_power_profile, test_power_profile_state_rsp)
{
	static const uint8_t req[] = {0x01, 0x31, 0x01};
	static const uint8_t rsp[] = {0x19, 0x31, 0x02, 0x02, 0x01, 0x01,
				      0x01, 0x03, 0x02, 0xff, 0x00, 0x00};

	zcl_cmd_answer = answer_state;
	answer_count = 2;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, rsp, sizeof(rsp));

	answer_count = ZZB_ZCL_POWER_PROFILE_RECORDS_MAX;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);

	answer_count = ZZB_ZCL_POWER_PROFILE_RECORDS_MAX + 1;
	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, req, sizeof(req));
	zassert_equal(zcl_cmd_respond_err, -EMSGSIZE);
}

/*
 * The commands of ZCL 3.17.6 reach zcl_cmd_cb of the client as received: PowerProfileNotification
 * of two phases, PowerProfileResponse of one phase, PowerProfileStateResponse of a running profile,
 * GetPowerProfilePrice, PowerProfilesStateNotification of two profiles, GetOverallSchedulePrice,
 * EnergyPhasesScheduleRequest, EnergyPhasesScheduleStateResponse without scheduled phases,
 * EnergyPhasesScheduleStateNotification of one phase, the schedule constraints, and
 * GetPowerProfilePriceExtended without and with PowerProfileStartTime
 */
ZTEST(zzb_zcl_power_profile, test_client_commands)
{
	static const struct test_cmd commands[] = {
		{0x00, 23, {0x01, 0x01, 0x02, 0x01, 0x01, 0x3c, 0x00, 0xd0, 0x07, 0x64, 0x00, 0xff,
			    0xff, 0x02, 0x02, 0x1e, 0x00, 0xf4, 0x01, 0x19, 0x00, 0x0a, 0x00}},
		{0x01,
		 13,
		 {0x01, 0x01, 0x01, 0x01, 0x01, 0x3c, 0x00, 0xd0, 0x07, 0x64, 0x00, 0xff, 0xff}},
		{0x02, 5, {0x01, 0x01, 0x01, 0x01, 0x03}},
		{0x03, 1, {0x01}},
		{0x04, 9, {0x02, 0x01, 0xff, 0x00, 0x00, 0x02, 0x01, 0x01, 0x05}},
		{0x05, 0, {0}},
		{0x06, 1, {0x01}},
		{0x07, 2, {0x01, 0x00}},
		{0x08, 5, {0x01, 0x01, 0x01, 0x3c, 0x00}},
		{0x09, 5, {0x01, 0x00, 0x00, 0xff, 0xff}},
		{0x0a, 5, {0x01, 0x0a, 0x00, 0xe0, 0x01}},
		{0x0b, 2, {0x00, 0x01}},
		{0x0b, 4, {0x01, 0x01, 0x1e, 0x00}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_client(c->id, c->payload, c->len, ZZB_ZCL_SUCCESS);
		check_passed(GATEWAY_EP, false, c);
	}
}

/*
 * A payload shorter than its fields gives MALFORMED_COMMAND: PowerProfileNotification without the
 * number of phases and with 9 octets of a phase, PowerProfileResponse without fields,
 * PowerProfileStateResponse without the count and with fewer records, GetPowerProfilePrice
 * without its field, PowerProfilesStateNotification with 3 octets of a record,
 * EnergyPhasesScheduleRequest without its field, the schedule state commands without the number
 * of phases and with 2 octets of a phase, the schedule constraints with 4 octets, and
 * GetPowerProfilePriceExtended with one octet and, with bit 0 of Options, without the 2 octets of
 * PowerProfileStartTime. An unknown command gives UNSUP_COMMAND. None reaches zcl_cmd_cb.
 */
ZTEST(zzb_zcl_power_profile, test_client_malformed)
{
	static const struct test_cmd commands[] = {
		{0x00, 2, {0x01, 0x01}},
		{0x00, 12, {0x01, 0x01, 0x01}},
		{0x01, 0, {0}},
		{0x02, 0, {0}},
		{0x02, 5, {0x02, 0x01, 0x01, 0x01, 0x03}},
		{0x03, 0, {0}},
		{0x04, 4, {0x01, 0x01, 0x01, 0x01}},
		{0x06, 0, {0}},
		{0x07, 1, {0x01}},
		{0x08, 4, {0x01, 0x01, 0x01, 0x3c}},
		{0x09, 4, {0x01, 0x00, 0x00, 0xff}},
		{0x0a, 4, {0x01, 0x0a, 0x00, 0xe0}},
		{0x0b, 1, {0x00}},
		{0x0b, 2, {0x01, 0x01}},
		{0x0b, 3, {0x01, 0x01, 0x1e}},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		reset_fakes();
		to_client(c->id, c->payload, c->len, ZZB_ZCL_MALFORMED_COMMAND);
		zassert_equal(zcl_cmd_calls.count, 0);
	}

	to_client(0x0c, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 0);
}

/*
 * ScheduleMode is 0 without records, is saved and reportable, bits 2 to 7 give INVALID_VALUE (ZCL
 * Table 3-137), and the reset sets it to 0
 */
ZTEST(zzb_zcl_power_profile, test_schedule_mode)
{
	static const uint8_t write_bad[] = {0x00, 0x60, 0x02, 0x04, 0x00, 0x18, 0x04};
	static const uint8_t write_bad_rsp[] = {0x18, 0x60, 0x04, 0x87, 0x04, 0x00};
	static const uint8_t write[] = {0x00, 0x61, 0x02, 0x04, 0x00, 0x18, 0x03};
	static const uint8_t write_rsp[] = {0x18, 0x61, 0x04, 0x00};
	static const uint8_t report[] = {0x00, 0x62, 0x06, 0x00, 0x04, 0x00,
					 0x18, 0x01, 0x00, 0x84, 0x03};
	static const uint8_t report_rsp[] = {0x18, 0x62, 0x07, 0x00};
	static const uint16_t reset_attrs[] = {ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE};

	power_profile140.schedule_mode = 0x01;
	zassert_ok(reboot());
	zassert_equal(power_profile140.schedule_mode, 0);

	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, write_bad, sizeof(write_bad));
	check_frame(0, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, write_bad_rsp, sizeof(write_bad_rsp));
	zassert_equal(power_profile140.schedule_mode, 0);

	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, write, sizeof(write));
	check_frame(1, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, write_rsp, sizeof(write_rsp));
	zassert_ok(reboot());
	zassert_equal(power_profile140.schedule_mode, 0x03);

	receive_ep(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, report, sizeof(report));
	check_frame(2, WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, report_rsp, sizeof(report_rsp));

	reset_fakes();
	reset_node();
	zassert_equal(power_profile140.schedule_mode, 0);
	check_zcl_cb(WHITE_GOODS_EP, ZZB_ZCL_POWER_PROFILE, reset_attrs, ARRAY_SIZE(reset_attrs));
}

ZTEST_SUITE(zzb_zcl_power_profile, NULL, setup, before, after, NULL);
