/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/window_covering.h>

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

/* Sends the command id with the payload to endpoint 50 and checks its Default Response */
static void command(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[8] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(COVERING_EP, ZZB_ZCL_WINDOW_COVERING, frame, 3 + len);
	check_frame(frames, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, rsp, sizeof(rsp));
}

/*
 * Up/Open, Down/Close and Stop go to zcl_cmd_cb as received, and the Default Response carries the
 * status it returns. A reserved command does not reach it.
 */
ZTEST(zzb_zcl_window_covering, test_commands)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	command(ZZB_ZCL_WINDOW_COVERING_UP_OPEN, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(zcl_cmd_calls.count, 1);
	zassert_equal(cmd->ep, COVERING_EP);
	zassert_equal(cmd->cluster, ZZB_ZCL_WINDOW_COVERING);
	zassert_equal(cmd->src_addr, REQUESTER_ADDR);
	zassert_equal(cmd->src_ep, REQUESTER_EP);
	zassert_equal(cmd->tsn, tsn);
	zassert_equal(cmd->id, ZZB_ZCL_WINDOW_COVERING_UP_OPEN);
	zassert_equal(cmd->len, 0);
	zassert_true(cmd->unicast);

	zcl_cmd_status = ZZB_ZCL_INVALID_FIELD;
	command(ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE, NULL, 0, ZZB_ZCL_INVALID_FIELD);
	command(ZZB_ZCL_WINDOW_COVERING_STOP, NULL, 0, ZZB_ZCL_INVALID_FIELD);
	zassert_equal(zcl_cmd_calls.count, 3);
	zassert_equal(zcl_cmd_calls.cmd[1].id, ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE);
	zassert_equal(zcl_cmd_calls.cmd[2].id, ZZB_ZCL_WINDOW_COVERING_STOP);

	command(0x03, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 3);
}

/*
 * Mode defaults to 0 (section 16), is saved, and bits 4 to 7 give INVALID_VALUE (ZCL Table 7-44).
 * The reset sets it to 0.
 */
ZTEST(zzb_zcl_window_covering, test_mode)
{
	static const uint8_t read[] = {0x00, 0x60, 0x00, 0x17, 0x00};
	static const uint8_t read_rsp[] = {0x18, 0x60, 0x01, 0x17, 0x00, 0x00, 0x18, 0x00};
	static const uint8_t write_bad[] = {0x00, 0x61, 0x02, 0x17, 0x00, 0x18, 0x11};
	static const uint8_t write_bad_rsp[] = {0x18, 0x61, 0x04, 0x87, 0x17, 0x00};
	static const uint8_t write[] = {0x00, 0x62, 0x02, 0x17, 0x00, 0x18, 0x0f};
	static const uint16_t mode[] = {ZZB_ZCL_WINDOW_COVERING_MODE};

	covering50.mode = 0x01;
	zassert_ok(reboot());
	receive_ep(COVERING_EP, ZZB_ZCL_WINDOW_COVERING, read, sizeof(read));
	check_frame(0, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, read_rsp, sizeof(read_rsp));

	receive_ep(COVERING_EP, ZZB_ZCL_WINDOW_COVERING, write_bad, sizeof(write_bad));
	check_frame(1, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, write_bad_rsp, sizeof(write_bad_rsp));
	zassert_equal(covering50.mode, 0);

	receive_ep(COVERING_EP, ZZB_ZCL_WINDOW_COVERING, write, sizeof(write));
	check_frame(2, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, (uint8_t[]){0x18, 0x62, 0x04, 0x00},
		    4);
	zassert_ok(reboot());
	zassert_equal(covering50.mode, 0x0f);

	reset_fakes();
	reset_node();
	zassert_equal(covering50.mode, 0);
	check_zcl_cb(COVERING_EP, ZZB_ZCL_WINDOW_COVERING, mode, ARRAY_SIZE(mode));
}

ZTEST_SUITE(zzb_zcl_window_covering, NULL, setup, before, after, NULL);
