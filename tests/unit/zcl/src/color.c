/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/color.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts with the defaults, without records */
static void before(void *fixture)
{
	zassert_ok(reboot());
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the Color Control command id with the payload to ep and checks its Default Response */
static void command(uint8_t ep, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[16] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(ep, ZZB_ZCL_COLOR, frame, 3 + len);
	check_frame(frames, ep, ZZB_ZCL_COLOR, rsp, sizeof(rsp));
}

/*
 * Sends the command id of cluster with the payload to ep with Disable Default Response, and checks
 * that it succeeds: an error status would still be answered
 */
static void send(uint8_t ep, uint16_t cluster, uint8_t id, const uint8_t *payload, size_t len)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[16] = {0x11, ++tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(ep, cluster, frame, 3 + len);
	zassert_equal(zzb_apsde_data_req_fake.call_count, frames, "command 0x%02x failed", id);
}

/* Move to Color as ZHA sends it, without OptionsMask and OptionsOverride */
static void move_to_color(uint8_t ep, uint16_t x, uint16_t y, uint16_t time)
{
	const uint8_t payload[] = {x & 0xff, x >> 8, y & 0xff, y >> 8, time & 0xff, time >> 8};

	send(ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_COLOR, payload, sizeof(payload));
}

/* Move to Color Temperature as ZHA sends it, without OptionsMask and OptionsOverride */
static void move_to_temperature(uint8_t ep, uint16_t mireds, uint16_t time)
{
	const uint8_t payload[] = {mireds & 0xff, mireds >> 8, time & 0xff, time >> 8};

	send(ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, payload, sizeof(payload));
}

static void set_u8(uint8_t ep, uint16_t attr, uint8_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_COLOR, attr, &value));
}

static void set_u16(uint8_t ep, uint16_t attr, uint16_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_COLOR, attr, &value));
}

static bool timer_armed(void)
{
	return k_work_delayable_busy_get(&test_node()->zcl.color_timer) != 0;
}

/* The number of zcl_cb calls for attribute attr of cluster on ep */
static int notified(uint8_t ep, uint16_t cluster, uint16_t attr)
{
	int n = 0;

	for (int i = 0; i < zcl_cb_calls.count; i++) {
		n += zcl_cb_calls.ep[i] == ep && zcl_cb_calls.cluster[i] == cluster &&
		     zcl_cb_calls.attr[i] == attr;
	}

	return n;
}

/* ZCL 5.2.2.2.1: the defaults, the writable attributes saved, the others read-only */
ZTEST(zzb_zcl_color, test_attributes)
{
	static const uint8_t read[] = {0x00, 0x80, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x07,
				       0x00, 0x08, 0x00, 0x0f, 0x00, 0x01, 0x40, 0x0a, 0x40, 0x0b,
				       0x40, 0x0c, 0x40, 0x0d, 0x40, 0x10, 0x40, 0xfd, 0xff};
	static const uint8_t read_rsp[] = {
		0x18, 0x80, 0x01, 0x02, 0x00, 0x00, 0x21, 0x00, 0x00, 0x03, 0x00, 0x00, 0x21,
		0x6b, 0x61, 0x04, 0x00, 0x00, 0x21, 0x7d, 0x60, 0x07, 0x00, 0x00, 0x21, 0xfa,
		0x00, 0x08, 0x00, 0x00, 0x30, 0x02, 0x0f, 0x00, 0x00, 0x18, 0x00, 0x01, 0x40,
		0x00, 0x30, 0x02, 0x0a, 0x40, 0x00, 0x19, 0x10, 0x00, 0x0b, 0x40, 0x00, 0x21,
		0x99, 0x00, 0x0c, 0x40, 0x00, 0x21, 0xf4, 0x01, 0x0d, 0x40, 0x00, 0x21, 0xc8,
		0x00, 0x10, 0x40, 0x00, 0x21, 0xff, 0xff, 0xfd, 0xff, 0x00, 0x21, 0x03, 0x00};
	static const uint8_t write[] = {0x00, 0x81, 0x02, 0x0f, 0x00, 0x18, 0x01,
					0x10, 0x40, 0x21, 0x00, 0x01, 0x03, 0x00,
					0x21, 0x00, 0x00, 0x08, 0x00, 0x30, 0x01};
	static const uint8_t write_rsp[] = {0x18, 0x81, 0x04, 0x88, 0x03, 0x00, 0x88, 0x08, 0x00};
	static const uint8_t reread[] = {0x00, 0x82, 0x00, 0x0f, 0x00, 0x10, 0x40};
	static const uint8_t reread_rsp[] = {0x18, 0x82, 0x01, 0x0f, 0x00, 0x00, 0x18,
					     0x01, 0x10, 0x40, 0x00, 0x21, 0x00, 0x01};

	receive_ep(41, ZZB_ZCL_COLOR, read, sizeof(read));
	check_frame(0, 41, ZZB_ZCL_COLOR, read_rsp, sizeof(read_rsp));
	receive_ep(41, ZZB_ZCL_COLOR, write, sizeof(write));
	check_frame(1, 41, ZZB_ZCL_COLOR, write_rsp, sizeof(write_rsp));

	zassert_ok(reboot());
	receive_ep(41, ZZB_ZCL_COLOR, reread, sizeof(reread));
	check_frame(2, 41, ZZB_ZCL_COLOR, reread_rsp, sizeof(reread_rsp));
}

/* ZCL 5.2.2.3.11: X and Y change together in XY mode and are saved */
ZTEST(zzb_zcl_color, test_move_to_color)
{
	static const uint16_t attrs[] = {ZZB_ZCL_COLOR_MODE, ZZB_ZCL_COLOR_ENHANCED_MODE,
					 ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_COLOR_CURRENT_Y};

	move_to_color(40, 0x2000, 0x3000, 0);
	zassert_equal(color40.current_x, 0x2000);
	zassert_equal(color40.current_y, 0x3000);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_XY);
	zassert_equal(color40.enhanced_color_mode, ZZB_ZCL_COLOR_MODE_XY);
	check_zcl_cb(40, ZZB_ZCL_COLOR, attrs, ARRAY_SIZE(attrs));

	zassert_ok(reboot());
	zassert_equal(color40.current_x, 0x2000);
	zassert_equal(color40.current_y, 0x3000);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_XY);

	/* X and Y end at 0xfeff (ZCL 5.2.2.2.1.4) */
	move_to_color(40, 0xffff, 0xff00, 0);
	zassert_equal(color40.current_x, 0xfeff);
	zassert_equal(color40.current_y, 0xfeff);

	command(40, ZZB_ZCL_COLOR_MOVE_TO_COLOR, (uint8_t[]){0x00, 0x10, 0x00, 0x10, 0x00}, 5,
		ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(color40.current_x, 0xfeff);
}

/* Section 16: X and Y move together every 100 ms, and only the end is saved */
ZTEST(zzb_zcl_color, test_transition)
{
	move_to_color(40, 0x1000, 0x2000, 0);
	move_to_color(40, 0x3000, 0x1000, 10);
	zassert_equal(color40.current_x, 0x1000);
	zassert_equal(color40.current_y, 0x2000);
	zassert_equal(color40.remaining_time, 10);
	zassert_true(timer_armed());

	k_sleep(K_MSEC(550));
	zassert_between_inclusive(color40.current_x, 0x1e00, 0x2200);
	zassert_between_inclusive(color40.current_y, 0x1700, 0x1900);
	zassert_between_inclusive(color40.remaining_time, 4, 6);

	/* The record holds the start values */
	zassert_ok(reboot());
	zassert_equal(color40.current_x, 0x1000);
	zassert_equal(color40.current_y, 0x2000);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());

	reset_fakes();
	move_to_color(40, 0x3000, 0x1000, 10);
	k_sleep(K_MSEC(1200));
	zassert_equal(color40.current_x, 0x3000);
	zassert_equal(color40.current_y, 0x1000);
	zassert_equal(color40.remaining_time, 0);
	zassert_between_inclusive(notified(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_Y), 9, 11);
	zassert_false(timer_armed());
	zassert_ok(reboot());
	zassert_equal(color40.current_x, 0x3000);
	zassert_equal(color40.current_y, 0x1000);
}

/* Section 16: a new command replaces the transition, and the reset of the node stops it */
ZTEST(zzb_zcl_color, test_transition_replaced)
{
	move_to_color(40, 0x3000, 0x1000, 10);
	k_sleep(K_MSEC(250));
	move_to_color(40, 0x2000, 0x2000, 0);
	zassert_equal(color40.current_x, 0x2000);
	zassert_equal(color40.current_y, 0x2000);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());

	move_to_color(40, 0x3000, 0x1000, 10);
	k_sleep(K_MSEC(250));
	reset_node();
	zassert_equal(color40.current_x, 0x616b);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());
}

/* ZCL 5.2.2.3.12: X and Y move at their rates until one of them reaches its limit */
ZTEST(zzb_zcl_color, test_move_color)
{
	move_to_color(40, 0xe000, 0x8000, 0);

	/* X needs 0x1eff / 0x2000 s, Y 8 s; Y moves 0x1eff / 2 on the same line */
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_COLOR, (uint8_t[]){0x00, 0x20, 0x00, 0xf0}, 4);
	zassert_equal(color40.remaining_time, 9);
	k_sleep(K_MSEC(1000));
	zassert_equal(color40.current_x, 0xfeff);
	zassert_equal(color40.current_y, 0x8000 - 0x1eff / 2);
	zassert_false(timer_armed());
	zassert_ok(reboot());
	zassert_equal(color40.current_x, 0xfeff);
	zassert_equal(color40.current_y, 0x8000 - 0x1eff / 2);

	/* Both rates 0 stop a transition, and the values are saved */
	move_to_color(40, 0x2000, 0x2000, 10);
	k_sleep(K_MSEC(350));
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_COLOR, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4);
	zassert_between_inclusive(color40.current_x, 0xa000, 0xc000);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());
	zassert_ok(reboot());
	zassert_between_inclusive(color40.current_x, 0xa000, 0xc000);

	command(40, ZZB_ZCL_COLOR_MOVE_COLOR, (uint8_t[]){0x00, 0x20, 0x00}, 3,
		ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 5.2.2.3.13: X and Y change by their steps over the transition time, within 0 to 0xfeff */
ZTEST(zzb_zcl_color, test_step_color)
{
	move_to_color(40, 0x2000, 0x0100, 0);
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STEP_COLOR,
	     (uint8_t[]){0x00, 0x10, 0x00, 0xfe, 0x05, 0x00}, 6);
	zassert_equal(color40.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color40.current_x, 0x3000);
	zassert_equal(color40.current_y, 0x0000);

	command(40, ZZB_ZCL_COLOR_STEP_COLOR, (uint8_t[]){0x00, 0x10, 0x00, 0xfe, 0x05}, 5,
		ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 5.2.2.3.14: the target is limited to the physical range with SUCCESS (ZCL 5.2.2.3.1) */
ZTEST(zzb_zcl_color, test_move_to_temperature)
{
	move_to_temperature(41, 300, 0);
	zassert_equal(color41.color_temperature, 300);
	check_zcl_cb(41, ZZB_ZCL_COLOR, (uint16_t[]){ZZB_ZCL_COLOR_TEMPERATURE}, 1);

	command(41, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, (uint8_t[]){0x58, 0x02, 0x00, 0x00}, 4,
		ZZB_ZCL_SUCCESS);
	zassert_equal(color41.color_temperature, COLOR_TEMP_MAX);
	command(41, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, (uint8_t[]){0x64, 0x00, 0x00, 0x00}, 4,
		ZZB_ZCL_SUCCESS);
	zassert_equal(color41.color_temperature, COLOR_TEMP_MIN);

	move_to_temperature(41, 400, 5);
	zassert_equal(color41.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color41.color_temperature, 400);
	zassert_ok(reboot());
	zassert_equal(color41.color_temperature, 400);

	/* The command sets color temperature mode */
	move_to_color(40, 0x5000, 0x5000, 0);
	move_to_temperature(40, 300, 0);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.enhanced_color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.color_temperature, 300);

	command(41, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, (uint8_t[]){0x2c, 0x01, 0x00}, 3,
		ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 5.2.2.3.21: a move at the rate in mireds per second ends at the limit of the command */
ZTEST(zzb_zcl_color, test_move_temperature)
{
	/* Up from 250 to the maximum 300 at 100 mireds per second */
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TEMPERATURE,
	     (uint8_t[]){0x01, 0x64, 0x00, 0x00, 0x00, 0x2c, 0x01}, 7);
	zassert_equal(color41.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color41.color_temperature, 300);

	/* A limit of 0 or beyond the physical range is the physical limit */
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TEMPERATURE,
	     (uint8_t[]){0x03, 0xf4, 0x01, 0x00, 0x00, 0x00, 0x00}, 7);
	k_sleep(K_MSEC(400));
	zassert_equal(color41.color_temperature, COLOR_TEMP_MIN);
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TEMPERATURE,
	     (uint8_t[]){0x01, 0xe8, 0x03, 0x00, 0x00, 0x58, 0x02}, 7);
	k_sleep(K_MSEC(500));
	zassert_equal(color41.color_temperature, COLOR_TEMP_MAX);

	/* Stop mode stops the transition */
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TEMPERATURE,
	     (uint8_t[]){0x03, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00}, 7);
	k_sleep(K_MSEC(250));
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TEMPERATURE,
	     (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7);
	zassert_between_inclusive(color41.color_temperature, 470, 490);
	zassert_equal(color41.remaining_time, 0);
	zassert_false(timer_armed());

	command(41, ZZB_ZCL_COLOR_MOVE_TEMPERATURE, (uint8_t[]){0x01, 0x00, 0x00, 0, 0, 0, 0}, 7,
		ZZB_ZCL_INVALID_FIELD);
	command(41, ZZB_ZCL_COLOR_MOVE_TEMPERATURE, (uint8_t[]){0x02, 0x10, 0x00, 0, 0, 0, 0}, 7,
		ZZB_ZCL_INVALID_FIELD);
	command(41, ZZB_ZCL_COLOR_MOVE_TEMPERATURE, (uint8_t[]){0x01, 0x10, 0x00, 0, 0, 0}, 6,
		ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 5.2.2.3.22: a step cut at a limit takes the same part of the transition time */
ZTEST(zzb_zcl_color, test_step_temperature)
{
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STEP_TEMPERATURE,
	     (uint8_t[]){0x01, 0x32, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}, 9);
	zassert_equal(color41.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color41.color_temperature, 300);

	/* 100 of 300 mireds to the maximum 400 */
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STEP_TEMPERATURE,
	     (uint8_t[]){0x01, 0x2c, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x90, 0x01}, 9);
	zassert_equal(color41.remaining_time, 3);
	k_sleep(K_MSEC(400));
	zassert_equal(color41.color_temperature, 400);

	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STEP_TEMPERATURE,
	     (uint8_t[]){0x03, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9);
	zassert_equal(color41.color_temperature, COLOR_TEMP_MIN);

	command(41, ZZB_ZCL_COLOR_STEP_TEMPERATURE, (uint8_t[]){0x00, 0x10, 0, 0, 0, 0, 0, 0, 0}, 9,
		ZZB_ZCL_INVALID_FIELD);
	command(41, ZZB_ZCL_COLOR_STEP_TEMPERATURE, (uint8_t[]){0x01, 0x10, 0, 0, 0, 0, 0, 0}, 8,
		ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(color41.color_temperature, COLOR_TEMP_MIN);
}

/* ZCL 5.2.2.2.1.18: commands of a capability that the light lacks or the stack does not have */
ZTEST(zzb_zcl_color, test_unsupported)
{
	command(41, ZZB_ZCL_COLOR_MOVE_TO_COLOR, (uint8_t[]){0x00, 0x10, 0x00, 0x10, 0x00, 0x00}, 6,
		ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(color41.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);

	command(40, ZZB_ZCL_COLOR_MOVE_TO_HUE, (uint8_t[]){0x10, 0x00, 0x00, 0x00}, 4,
		ZZB_ZCL_UNSUP_COMMAND);
	command(40, ZZB_ZCL_COLOR_ENHANCED_MOVE_TO_HUE, (uint8_t[]){0x00, 0x10, 0x00, 0x00, 0x00},
		5, ZZB_ZCL_UNSUP_COMMAND);
	command(40, ZZB_ZCL_COLOR_LOOP_SET, (uint8_t[]){0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
		7, ZZB_ZCL_UNSUP_COMMAND);
	command(40, 0x4d, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
}

/* Section 16: the reset of the node restores the defaults with zcl_cb */
ZTEST(zzb_zcl_color, test_reset)
{
	move_to_color(40, 0x2000, 0x3000, 0);
	set_u8(40, ZZB_ZCL_COLOR_OPTIONS, 0x01);
	set_u16(40, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, 0x0100);
	reset_fakes();

	reset_node();
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.enhanced_color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.current_x, 0x616b);
	zassert_equal(color40.current_y, 0x607d);
	zassert_equal(color40.color_temperature, 0x00fa);
	zassert_equal(color40.options, 0);
	zassert_equal(color40.start_up_color_temperature, 0xffff);
	check_zcl_cb(40, ZZB_ZCL_COLOR,
		     (uint16_t[]){ZZB_ZCL_COLOR_MODE, ZZB_ZCL_COLOR_ENHANCED_MODE,
				  ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_COLOR_CURRENT_Y,
				  ZZB_ZCL_COLOR_OPTIONS, ZZB_ZCL_COLOR_START_UP_TEMPERATURE},
		     6);

	zassert_ok(reboot());
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.current_x, 0x616b);
}

ZTEST_SUITE(zzb_zcl_color, NULL, setup, before, after, NULL);
