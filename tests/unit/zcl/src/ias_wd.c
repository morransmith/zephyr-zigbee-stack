/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/ias_wd.h>

#include "fakes.h"

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

/* Sends the command id with the payload to endpoint ep and checks its Default Response */
static void command(uint8_t ep, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[9] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(ep, ZZB_ZCL_IAS_WD, frame, 3 + len);
	check_frame(frames, ep, ZZB_ZCL_IAS_WD, rsp, sizeof(rsp));
}

/* Sends Start Warning with the octet of the fields, the duration and the duty cycle to ep */
static void start_warning(uint8_t ep, uint8_t fields, uint16_t duration, uint8_t duty_cycle,
			  uint8_t status)
{
	const uint8_t payload[] = {fields, duration & 0xff, duration >> 8, duty_cycle, 0x02};

	command(ep, ZZB_ZCL_IAS_WD_START_WARNING, payload, sizeof(payload), status);
}

/* Checks that call i of zcl_cmd_cb got the command id of endpoint ep with the payload */
static void check_cmd(int i, uint8_t ep, uint8_t id, const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[i];

	zassert_true(zcl_cmd_calls.count > i);
	zassert_equal(cmd->ep, ep);
	zassert_equal(cmd->cluster, ZZB_ZCL_IAS_WD);
	zassert_equal(cmd->id, id);
	zassert_equal(cmd->len, len);
	zassert_mem_equal(zcl_cmd_calls.payload[i], payload, len);
}

/* Checks that call i of zcl_cmd_cb got Start Warning on ep with the fields, duration and cycle */
static void check_warning(int i, uint8_t ep, uint8_t fields, uint16_t duration, uint8_t duty_cycle)
{
	const uint8_t payload[] = {fields, duration & 0xff, duration >> 8, duty_cycle, 0x02};

	check_cmd(i, ep, ZZB_ZCL_IAS_WD_START_WARNING, payload, sizeof(payload));
}

/* ZCL Table 8-19: MaxDuration defaults to 240 s, is saved, and 0xffff gives INVALID_VALUE */
ZTEST(zzb_zcl_ias_wd, test_max_duration)
{
	static const uint8_t read[] = {0x00, 0x60, 0x00, 0x00, 0x00};
	static const uint8_t read_rsp[] = {0x18, 0x60, 0x01, 0x00, 0x00, 0x00, 0x21, 0xf0, 0x00};
	static const uint8_t write_bad[] = {0x00, 0x61, 0x02, 0x00, 0x00, 0x21, 0xff, 0xff};
	static const uint8_t write_bad_rsp[] = {0x18, 0x61, 0x04, 0x87, 0x00, 0x00};
	static const uint8_t write[] = {0x00, 0x62, 0x02, 0x00, 0x00, 0x21, 0xfe, 0xff};
	static const uint16_t max_duration[] = {ZZB_ZCL_IAS_WD_MAX_DURATION};

	wd85.max_duration = 10;
	zassert_ok(reboot());
	receive_ep(WD_EP, ZZB_ZCL_IAS_WD, read, sizeof(read));
	check_frame(0, WD_EP, ZZB_ZCL_IAS_WD, read_rsp, sizeof(read_rsp));

	receive_ep(WD_EP, ZZB_ZCL_IAS_WD, write_bad, sizeof(write_bad));
	check_frame(1, WD_EP, ZZB_ZCL_IAS_WD, write_bad_rsp, sizeof(write_bad_rsp));
	zassert_equal(wd85.max_duration, 240);

	receive_ep(WD_EP, ZZB_ZCL_IAS_WD, write, sizeof(write));
	check_frame(2, WD_EP, ZZB_ZCL_IAS_WD, (uint8_t[]){0x18, 0x62, 0x04, 0x00}, 4);
	zassert_ok(reboot());
	zassert_equal(wd85.max_duration, 0xfffe);

	reset_fakes();
	reset_node();
	zassert_equal(wd85.max_duration, 240);
	check_zcl_cb(WD_EP, ZZB_ZCL_IAS_WD, max_duration, ARRAY_SIZE(max_duration));
}

/*
 * ZCL 8.4.2.2.1: Start Warning reaches zcl_cmd_cb as a copy of its 5 octets with the warning
 * duration limited to MaxDuration and the strobe duty cycle rounded to the nearest multiple of 10,
 * at most 100. The Default Response carries the status of zcl_cmd_cb.
 */
ZTEST(zzb_zcl_ias_wd, test_start_warning)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	/* Emergency with strobe and siren level 2 */
	start_warning(WD_EP, 0x36, 5, 44, ZZB_ZCL_SUCCESS);
	zassert_equal(zcl_cmd_calls.count, 1);
	zassert_equal(cmd->src_addr, REQUESTER_ADDR);
	zassert_equal(cmd->src_ep, REQUESTER_EP);
	zassert_equal(cmd->tsn, tsn);
	zassert_true(cmd->unicast);
	zassert_true(cmd->server);
	check_warning(0, WD_EP, 0x36, 5, 40);

	zcl_cmd_status = ZZB_ZCL_FAILURE;
	start_warning(WD_EP, 0x10, 300, 45, ZZB_ZCL_FAILURE);
	check_warning(1, WD_EP, 0x10, 240, 50);

	/* Octets after the fields are not passed */
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING,
		(uint8_t[]){0x10, 0x05, 0x00, 0xff, 0x02, 0xaa}, 6, ZZB_ZCL_FAILURE);
	check_warning(2, WD_EP, 0x10, 5, 100);
}

/* The application can answer the copy with zzb_zcl_cmd_respond() */
ZTEST(zzb_zcl_ias_wd, test_start_warning_respond)
{
	static const uint8_t req[] = {0x01, 0x30, 0x00, 0x10, 0x05, 0x00, 0x00, 0x02};

	zcl_cmd_responds = true;
	receive_ep(WD_EP, ZZB_ZCL_IAS_WD, req, sizeof(req));
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_frame(0, WD_EP, ZZB_ZCL_IAS_WD, (uint8_t[]){0x19, 0x30, 0x42, 0xaa}, 4);
}

/*
 * ZCL 8.4.2.2.1: a warning mode above 6, a strobe above 1 or a strobe level above 3 gives
 * INVALID_FIELD, a payload shorter than 5 octets MALFORMED_COMMAND
 */
ZTEST(zzb_zcl_ias_wd, test_start_warning_checks)
{
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING, (uint8_t[]){0x67, 0x05, 0x00, 0x00, 0x03}, 5,
		ZZB_ZCL_SUCCESS);
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING, (uint8_t[]){0x70, 0x05, 0x00, 0x00, 0x00}, 5,
		ZZB_ZCL_INVALID_FIELD);
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING, (uint8_t[]){0x18, 0x05, 0x00, 0x00, 0x00}, 5,
		ZZB_ZCL_INVALID_FIELD);
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING, (uint8_t[]){0x10, 0x05, 0x00, 0x00, 0x04}, 5,
		ZZB_ZCL_INVALID_FIELD);
	command(WD_EP, ZZB_ZCL_IAS_WD_START_WARNING, (uint8_t[]){0x10, 0x05, 0x00, 0x00}, 4,
		ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 1);
}

/*
 * ZCL 8.4.2.2.2: Squawk reaches zcl_cmd_cb as received, and a squawk mode above 1 gives
 * INVALID_FIELD. Other commands are not supported.
 */
ZTEST(zzb_zcl_ias_wd, test_squawk)
{
	command(WD_EP, ZZB_ZCL_IAS_WD_SQUAWK, (uint8_t[]){0x1b}, 1, ZZB_ZCL_SUCCESS);
	command(WD_EP, ZZB_ZCL_IAS_WD_SQUAWK, (uint8_t[]){0x20}, 1, ZZB_ZCL_INVALID_FIELD);
	command(WD_EP, ZZB_ZCL_IAS_WD_SQUAWK, NULL, 0, ZZB_ZCL_MALFORMED_COMMAND);
	command(WD_EP, 0x02, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 1);
	check_cmd(0, WD_EP, ZZB_ZCL_IAS_WD_SQUAWK, (uint8_t[]){0x1b}, 1);
}

ZTEST_SUITE(zzb_zcl_ias_wd, NULL, setup, before, after, NULL);
