/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/scenes.h>
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
	covering50.window_covering_type = 0x08;
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

/* Stores scene id of group 0 on endpoint 50 with Store Scene */
static void store_scene(uint8_t id)
{
	const uint8_t rsp[] = {0x19, ++tsn, 0x04, 0x00, 0x00, 0x00, id};
	int frames = zzb_apsde_data_req_fake.call_count;

	receive_ep(COVERING_EP, ZZB_ZCL_SCENES, (uint8_t[]){0x01, tsn, 0x04, 0x00, 0x00, id}, 6);
	check_frame(frames, COVERING_EP, ZZB_ZCL_SCENES, rsp, sizeof(rsp));
}

/* Recalls scene id of group 0 on endpoint 50 with Recall Scene and a transition time of 5 s */
static void recall_scene(uint8_t id)
{
	const uint8_t req[] = {0x01, ++tsn, 0x05, 0x00, 0x00, id, 0x32, 0x00};
	const uint8_t rsp[] = {0x18, tsn, 0x0b, 0x05, 0x00};
	int frames = zzb_apsde_data_req_fake.call_count;

	receive_ep(COVERING_EP, ZZB_ZCL_SCENES, req, sizeof(req));
	check_frame(frames, COVERING_EP, ZZB_ZCL_SCENES, rsp, sizeof(rsp));
}

/* The application sets CurrentPositionLiftPercentage or CurrentPositionTiltPercentage */
static void set_position(uint16_t attr, uint8_t percentage)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), COVERING_EP, ZZB_ZCL_WINDOW_COVERING, attr,
				    &percentage));
}

/* Checks that call i of zcl_cmd_cb got the command id of endpoint 50 with the payload */
static void check_cmd(int i, uint8_t id, const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[i];

	zassert_true(zcl_cmd_calls.count > i);
	zassert_equal(cmd->ep, COVERING_EP);
	zassert_equal(cmd->cluster, ZZB_ZCL_WINDOW_COVERING);
	zassert_equal(cmd->id, id);
	zassert_equal(cmd->len, len);
	zassert_mem_equal(zcl_cmd_calls.payload[i], payload, len);
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
 * ZCL 7.4.2.2.5, 7.4.2.2.7: Go To Lift Percentage and Go To Tilt Percentage reach zcl_cmd_cb as
 * received, and a percentage above 100 gives INVALID_VALUE
 */
ZTEST(zzb_zcl_window_covering, test_go_to_percentage)
{
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){50}, 1, ZZB_ZCL_SUCCESS);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){100}, 1,
		ZZB_ZCL_SUCCESS);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){101}, 1,
		ZZB_ZCL_INVALID_VALUE);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){0xff}, 1,
		ZZB_ZCL_INVALID_VALUE);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, NULL, 0, ZZB_ZCL_MALFORMED_COMMAND);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, NULL, 0, ZZB_ZCL_MALFORMED_COMMAND);

	zassert_equal(zcl_cmd_calls.count, 2);
	check_cmd(0, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){50}, 1);
	check_cmd(1, ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){100}, 1);
}

/*
 * ZCL 7.4.2.2.4, 7.4.2.2.6: a value between the installed open and closed limits, in either order,
 * reaches zcl_cmd_cb as received, and another value gives INVALID_VALUE. The open limit of tilt
 * is above its closed limit, as the text of ZCL assumes.
 */
ZTEST(zzb_zcl_window_covering, test_go_to_value)
{
	covering50.installed_open_limit_lift = 10;
	covering50.installed_closed_limit_lift = 200;
	covering50.installed_open_limit_tilt = 900;
	covering50.installed_closed_limit_tilt = 0;

	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0x0a, 0x00}, 2,
		ZZB_ZCL_SUCCESS);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0xc8, 0x00}, 2,
		ZZB_ZCL_SUCCESS);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0x09, 0x00}, 2,
		ZZB_ZCL_INVALID_VALUE);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0xc9, 0x00}, 2,
		ZZB_ZCL_INVALID_VALUE);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE, (uint8_t[]){0x84, 0x03}, 2,
		ZZB_ZCL_SUCCESS);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE, (uint8_t[]){0x85, 0x03}, 2,
		ZZB_ZCL_INVALID_VALUE);
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE, (uint8_t[]){0x00}, 1,
		ZZB_ZCL_MALFORMED_COMMAND);

	zassert_equal(zcl_cmd_calls.count, 3);
	check_cmd(0, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0x0a, 0x00}, 2);
	check_cmd(1, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, (uint8_t[]){0xc8, 0x00}, 2);
	check_cmd(2, ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE, (uint8_t[]){0x84, 0x03}, 2);
}

/*
 * WindowCoveringType gives the axes, as in ZHA 2.3.1: tilt with 0x06, 0x07 and 0x08, lift with
 * every type except 0x06 and 0x07. A command of a missing axis gives UNSUP_COMMAND, also before
 * the check of its payload.
 */
ZTEST(zzb_zcl_window_covering, test_axes)
{
	static const uint8_t lift[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x81, 0x00, 0x00};
	static const uint8_t tilt[] = {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x00, 0x00, 0x00, 0x81};

	ARRAY_FOR_EACH(lift, type) {
		reset_fakes();
		covering50.window_covering_type = type;
		command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){0}, 1,
			lift[type]);
		command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){0}, 1,
			tilt[type]);
	}

	covering50.window_covering_type = 0x07;
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	covering50.window_covering_type = 0x00;
	command(ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
}

/*
 * ZCL 7.4.2.4: Store Scene stores the positions that the application set, and Recall Scene passes
 * them to zcl_cmd_cb as Go To Lift Percentage and Go To Tilt Percentage without a source, ignoring
 * the transition time and the status of zcl_cmd_cb (section 16). Setting a position makes
 * SceneValid FALSE.
 */
ZTEST(zzb_zcl_window_covering, test_scene)
{
	static const uint8_t view[] = {0x01, 0x70, 0x01, 0x00, 0x00, 0x01};
	static const uint8_t view_rsp[] = {0x19, 0x70, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
					   0x00, 0x00, 0x02, 0x01, 0x02, 0x1e, 0x3c};

	set_position(ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE, 30);
	set_position(ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE, 60);
	store_scene(1);
	receive_ep(COVERING_EP, ZZB_ZCL_SCENES, view, sizeof(view));
	check_frame(1, COVERING_EP, ZZB_ZCL_SCENES, view_rsp, sizeof(view_rsp));

	set_position(ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE, 80);
	zassert_false(scenes50.scene_valid);

	zcl_cmd_status = ZZB_ZCL_INVALID_FIELD;
	zcl_cmd_responds = true;
	recall_scene(1);
	zassert_equal(zcl_cmd_respond_err, -EINVAL);
	zassert_equal(zcl_cmd_calls.count, 2);
	check_cmd(0, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){30}, 1);
	check_cmd(1, ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){60}, 1);
	zassert_false(zcl_cmd_calls.cmd[0].unicast);
	zassert_true(zcl_cmd_calls.cmd[0].server);
	zassert_equal(zcl_cmd_calls.cmd[0].src_addr, 0);
	zassert_equal(zcl_cmd_calls.cmd[0].src_ep, 0);
	zassert_equal(zcl_cmd_calls.cmd[0].tsn, 0);
	zassert_equal(covering50.current_position_lift_percentage, 80);
	zassert_true(scenes50.scene_valid);
}

/* A recall skips an axis that the type lacks and an unknown position */
ZTEST(zzb_zcl_window_covering, test_scene_axes)
{
	set_position(ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE, 30);
	set_position(ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE, 0xff);
	store_scene(1);
	recall_scene(1);
	zassert_equal(zcl_cmd_calls.count, 1);
	check_cmd(0, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, (uint8_t[]){30}, 1);

	reset_fakes();
	covering50.window_covering_type = 0x07;
	set_position(ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE, 60);
	store_scene(2);
	recall_scene(2);
	zassert_equal(zcl_cmd_calls.count, 1);
	check_cmd(0, ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, (uint8_t[]){60}, 1);
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
