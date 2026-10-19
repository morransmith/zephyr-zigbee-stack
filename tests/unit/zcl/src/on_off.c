/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/on_off.h>

#include "fakes.h"

#define OFF                         0x00
#define ON                          0x01
#define TOGGLE                      0x02
#define OFF_WITH_EFFECT             0x40
#define ON_WITH_RECALL_GLOBAL_SCENE 0x41

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts off, without records */
static void before(void *fixture)
{
	on_off.start_up_default = ZZB_ZCL_START_UP_PREVIOUS;
	zassert_ok(reboot());
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the On/Off command id with the payload to endpoint 2 and checks its Default Response */
static void command_with(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[8] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(2, ZZB_ZCL_ON_OFF, frame, 3 + len);
	check_frame(frames, 2, ZZB_ZCL_ON_OFF, rsp, sizeof(rsp));
}

static void command(uint8_t id)
{
	command_with(id, NULL, 0, ZZB_ZCL_SUCCESS);
}

static void set(uint16_t attr, uint16_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), 2, ZZB_ZCL_ON_OFF, attr, &value));
}

/* The attributes that zcl_cb reported from call i on */
static void check_notified(int i, const uint16_t *attrs, int count)
{
	zassert_equal(zcl_cb_calls.count, i + count);
	for (int j = 0; j < count; j++) {
		zassert_equal(zcl_cb_calls.ep[i + j], 2);
		zassert_equal(zcl_cb_calls.cluster[i + j], ZZB_ZCL_ON_OFF);
		zassert_equal(zcl_cb_calls.attr[i + j], attrs[j]);
	}
}

/* ZCL 3.8.2.3.1, 3.8.2.3.2 */
ZTEST(zzb_zcl_on_off, test_on_and_off)
{
	command(ON);
	zassert_true(on_off.on_off);
	zassert_true(on_off.global_scene_control);
	check_notified(0, (uint16_t[]){ZZB_ZCL_ON_OFF_ON_OFF}, 1);

	/* On a node that is on, nothing changes */
	command(ON);
	zassert_equal(zcl_cb_calls.count, 1);

	set(ZZB_ZCL_ON_OFF_ON_TIME, 30);
	command(OFF);
	zassert_false(on_off.on_off);
	zassert_equal(on_off.on_time, 0);
	check_notified(1, (uint16_t[]){ZZB_ZCL_ON_OFF_ON_OFF, ZZB_ZCL_ON_OFF_ON_TIME}, 2);
}

/* ZCL 3.8.2.3.2: On clears OffWaitTime only when OnTime is 0 */
ZTEST(zzb_zcl_on_off, test_on_clears_off_wait_time)
{
	set(ZZB_ZCL_ON_OFF_ON_TIME, 30);
	set(ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 20);
	command(ON);
	zassert_equal(on_off.off_wait_time, 20);

	command(OFF);
	command(ON);
	zassert_equal(on_off.off_wait_time, 0);
}

/* ZCL 3.8.2.3.3 */
ZTEST(zzb_zcl_on_off, test_toggle)
{
	set(ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 20);
	command(TOGGLE);
	zassert_true(on_off.on_off);
	zassert_equal(on_off.off_wait_time, 0);

	set(ZZB_ZCL_ON_OFF_ON_TIME, 30);
	command(TOGGLE);
	zassert_false(on_off.on_off);
	zassert_equal(on_off.on_time, 0);
}

/* Section 15: OnOff is saved */
ZTEST(zzb_zcl_on_off, test_saved)
{
	command(ON);
	zassert_ok(reboot());
	zassert_true(on_off.on_off);
}

/* ZCL 3.8.2.2: OnOff and GlobalSceneControl are read-only */
ZTEST(zzb_zcl_on_off, test_attributes)
{
	static const uint8_t read[] = {0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x40,
				       0x01, 0x40, 0x02, 0x40, 0xfd, 0xff};
	static const uint8_t read_rsp[] = {0x18, 0x90, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00,
					   0x00, 0x40, 0x00, 0x10, 0x01, 0x01, 0x40, 0x00,
					   0x21, 0x00, 0x00, 0x02, 0x40, 0x00, 0x21, 0x00,
					   0x00, 0xfd, 0xff, 0x00, 0x21, 0x02, 0x00};
	static const uint8_t write[] = {0x00, 0x91, 0x02, 0x00, 0x00, 0x10, 0x01, 0x00,
					0x40, 0x10, 0x00, 0x01, 0x40, 0x21, 0x0a, 0x00};
	static const uint8_t write_rsp[] = {0x18, 0x91, 0x04, 0x88, 0x00, 0x00, 0x88, 0x00, 0x40};

	receive_ep(2, ZZB_ZCL_ON_OFF, read, sizeof(read));
	check_frame(0, 2, ZZB_ZCL_ON_OFF, read_rsp, sizeof(read_rsp));

	receive_ep(2, ZZB_ZCL_ON_OFF, write, sizeof(write));
	check_frame(1, 2, ZZB_ZCL_ON_OFF, write_rsp, sizeof(write_rsp));
	zassert_false(on_off.on_off);
	zassert_equal(on_off.on_time, 10);
}

/* Sets StartUpOnOff and OnOff, then checks OnOff after a reboot */
static void check_start_up(uint8_t start_up, bool before, bool after)
{
	set(ZZB_ZCL_ON_OFF_START_UP, start_up);
	command(before ? ON : OFF);
	zassert_ok(reboot());
	zassert_equal(on_off.on_off, after, "StartUpOnOff 0x%02x", start_up);
}

/* ZCL 3.8.2.2.5 */
ZTEST(zzb_zcl_on_off, test_start_up)
{
	check_start_up(ZZB_ZCL_START_UP_OFF, true, false);
	check_start_up(ZZB_ZCL_START_UP_ON, false, true);
	check_start_up(ZZB_ZCL_START_UP_PREVIOUS, true, true);
	check_start_up(ZZB_ZCL_START_UP_PREVIOUS, false, false);

	/* The result of the toggle is saved, so the next boot toggles it again */
	check_start_up(ZZB_ZCL_START_UP_TOGGLE, true, false);
	zassert_ok(reboot());
	zassert_true(on_off.on_off);

	/* Only the commands that changed OnOff, none of the boots */
	zassert_equal(zcl_cb_calls.count, 3);
}

/* Without a record StartUpOnOff takes the default of the application */
ZTEST(zzb_zcl_on_off, test_start_up_default)
{
	on_off.start_up_default = ZZB_ZCL_START_UP_ON;
	reset_node();
	zassert_ok(reboot());
	zassert_equal(on_off.start_up_on_off, ZZB_ZCL_START_UP_ON);
	zassert_true(on_off.on_off);
}

/* The reset restores the default of StartUpOnOff without applying it */
ZTEST(zzb_zcl_on_off, test_start_up_reset)
{
	set(ZZB_ZCL_ON_OFF_START_UP, ZZB_ZCL_START_UP_ON);
	command(ON);
	reset_fakes();
	reset_node();
	zassert_equal(on_off.start_up_on_off, ZZB_ZCL_START_UP_PREVIOUS);
	zassert_false(on_off.on_off);
	check_notified(0, (uint16_t[]){ZZB_ZCL_ON_OFF_ON_OFF, ZZB_ZCL_ON_OFF_START_UP}, 2);
}

/* ZCL Table 3-46: reserved values are rejected */
ZTEST(zzb_zcl_on_off, test_start_up_write)
{
	static const uint8_t write[] = {0x00, 0x92, 0x02, 0x03, 0x40, 0x30, 0x03};
	static const uint8_t write_rsp[] = {0x18, 0x92, 0x04, 0x87, 0x03, 0x40};
	static const uint8_t toggle[] = {0x00, 0x93, 0x02, 0x03, 0x40, 0x30, 0x02};

	receive_ep(2, ZZB_ZCL_ON_OFF, write, sizeof(write));
	check_frame(0, 2, ZZB_ZCL_ON_OFF, write_rsp, sizeof(write_rsp));
	receive_ep(2, ZZB_ZCL_ON_OFF, toggle, sizeof(toggle));
	check_frame(1, 2, ZZB_ZCL_ON_OFF, (uint8_t[]){0x18, 0x93, 0x04, 0x00}, 4);
	zassert_equal(on_off.start_up_on_off, ZZB_ZCL_START_UP_TOGGLE);
}

/* ZCL 3.8.2.3.4: the effect goes to the application, the global scene keeps OnOff */
ZTEST(zzb_zcl_on_off, test_off_with_effect)
{
	command(ON);
	set(ZZB_ZCL_ON_OFF_ON_TIME, 30);
	reset_fakes();

	command_with(OFF_WITH_EFFECT, (uint8_t[]){0x00, 0x01}, 2, ZZB_ZCL_SUCCESS);
	zassert_equal(effect_calls.count, 1);
	zassert_equal(effect_calls.ep, 2);
	zassert_equal(effect_calls.cluster, ZZB_ZCL_ON_OFF);
	zassert_equal(effect_calls.effect, 0x00);
	zassert_equal(effect_calls.variant, 0x01);
	zassert_equal(effect_calls.zcl_cb_count, 0);
	zassert_false(on_off.global_scene_control);
	zassert_false(on_off.on_off);
	check_notified(0,
		       (uint16_t[]){ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL, ZZB_ZCL_ON_OFF_ON_OFF,
				    ZZB_ZCL_ON_OFF_ON_TIME},
		       3);

	/* With GlobalSceneControl FALSE only OnOff changes, and OnTime keeps its value */
	set(ZZB_ZCL_ON_OFF_ON_TIME, 30);
	command_with(OFF_WITH_EFFECT, (uint8_t[]){0x01, 0x00}, 2, ZZB_ZCL_SUCCESS);
	zassert_false(on_off.on_off);
	zassert_false(on_off.global_scene_control);
	zassert_equal(on_off.on_time, 30);

	command_with(OFF_WITH_EFFECT, (uint8_t[]){0x01}, 1, ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(effect_calls.count, 2);
}

/* ZCL 3.8.2.3.5 */
ZTEST(zzb_zcl_on_off, test_on_with_recall_global_scene)
{
	/* GlobalSceneControl TRUE discards the command */
	command(ON_WITH_RECALL_GLOBAL_SCENE);
	zassert_false(on_off.on_off);

	command(ON);
	command_with(OFF_WITH_EFFECT, (uint8_t[]){0x00, 0x00}, 2, ZZB_ZCL_SUCCESS);
	set(ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 20);
	reset_fakes();
	command(ON_WITH_RECALL_GLOBAL_SCENE);
	zassert_true(on_off.on_off);
	zassert_true(on_off.global_scene_control);
	zassert_equal(on_off.off_wait_time, 0);
	check_notified(0,
		       (uint16_t[]){ZZB_ZCL_ON_OFF_ON_OFF, ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL,
				    ZZB_ZCL_ON_OFF_OFF_WAIT_TIME},
		       3);

	/* A scene stored while off recalls off */
	command(OFF);
	command_with(OFF_WITH_EFFECT, (uint8_t[]){0x00, 0x00}, 2, ZZB_ZCL_SUCCESS);
	command(ON_WITH_RECALL_GLOBAL_SCENE);
	zassert_false(on_off.on_off);
	zassert_true(on_off.global_scene_control);
}

ZTEST_SUITE(zzb_zcl_on_off, NULL, setup, before, after, NULL);
