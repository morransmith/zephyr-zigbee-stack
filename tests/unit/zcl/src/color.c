/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void set_on_off(uint8_t ep, bool on)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on));
}

/* Starts again from the records, as zzb_node_init() does, and turns the lights on */
static void restart(void)
{
	zassert_ok(reboot());
	set_on_off(40, true);
	set_on_off(41, true);
}

/* Each test starts with the defaults, without records, and the lights on */
static void before(void *fixture)
{
	color41.start_up_default = 0xffff;
	zassert_ok(reboot());
	reset_node();
	set_on_off(40, true);
	set_on_off(41, true);
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

	restart();
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

	restart();
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
	restart();
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
	restart();
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
	zassert_equal(color40.color_temperature, 0x00fa);
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
	restart();
	zassert_equal(color40.current_x, 0xfeff);
	zassert_equal(color40.current_y, 0x8000 - 0x1eff / 2);

	/* Both rates 0 stop a transition, and the values are saved */
	move_to_color(40, 0x2000, 0x2000, 10);
	k_sleep(K_MSEC(350));
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_COLOR, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4);
	zassert_between_inclusive(color40.current_x, 0xa000, 0xc000);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());
	restart();
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
	restart();
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

/*
 * Section 16: with the XY capability, X and Y follow the color temperature in its mode. The
 * expected values are x and y of Kim et al. (2002) times 65536, computed in double precision:
 * 250 mireds (4000 K) x 0.380528 y 0.376734, 370 mireds (2702.7 K) x 0.459094 y 0.410611 and
 * 500 mireds (2000 K) x 0.526903 y 0.413265.
 */
ZTEST(zzb_zcl_color, test_temperature_xy)
{
	/* Without records the start hook computes them from the default 250 mireds */
	zassert_equal(color40.current_x, 0x616a);
	zassert_equal(color40.current_y, 0x6072);

	/* The changed hook writes them before zcl_cb reports the temperature */
	move_to_temperature(40, 370, 0);
	zassert_equal(color40.current_x, 0x7587);
	zassert_equal(color40.current_y, 0x691e);
	check_zcl_cb(40, ZZB_ZCL_COLOR,
		     (uint16_t[]){ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_COLOR_CURRENT_Y,
				  ZZB_ZCL_COLOR_TEMPERATURE},
		     3);

	/* Also during a transition */
	reset_fakes();
	move_to_temperature(40, 500, 10);
	k_sleep(K_MSEC(1100));
	zassert_equal(color40.current_x, 0x86e3);
	zassert_equal(color40.current_y, 0x69cc);
	zassert_between_inclusive(notified(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_X), 9, 11);

	/* X and Y are not saved; the start hook computes them from the saved temperature */
	reset_fakes();
	restart();
	zassert_equal(color40.current_x, 0x86e3);
	zassert_equal(color40.current_y, 0x69cc);
	zassert_equal(zcl_cb_calls.count, 0);

	/* Without the XY capability they stay */
	move_to_temperature(41, 400, 0);
	zassert_equal(color41.current_x, 0x616b);
	zassert_equal(color41.current_y, 0x607d);
}

/*
 * ZCL 5.2.2.3.2: from XY mode the color temperature starts at the temperature of x and y by the
 * formula of McCamy (1992), within the physical range. Computed in double precision: x 0.312698
 * y 0.328995 give 6505.2 K (153.7 mireds), x 0.600006 y 0.350006 548.9 mireds and x 0.169998
 * y 0.699997 124.5 mireds. X and Y follow by Kim et al.: 154 mireds x 0.313595 y 0.323761,
 * 400 mireds x 0.476459 y 0.413716.
 */
ZTEST(zzb_zcl_color, test_temperature_from_xy)
{
	move_to_color(40, 0x500d, 0x5439, 0);
	move_to_temperature(40, 400, 10);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.color_temperature, 154);
	zassert_equal(color40.current_x, 0x5048);
	zassert_equal(color40.current_y, 0x52e2);
	k_sleep(K_MSEC(1100));
	zassert_equal(color40.color_temperature, 400);
	zassert_equal(color40.current_x, 0x79f9);
	zassert_equal(color40.current_y, 0x69e9);

	reset_fakes();
	move_to_color(40, 0x999a, 0x599a, 0);
	move_to_temperature(40, 300, 10);
	zassert_equal(color40.color_temperature, COLOR_TEMP_MAX);
	move_to_color(40, 0x2b85, 0xb333, 0);
	move_to_temperature(40, 300, 10);
	zassert_equal(color40.color_temperature, COLOR_TEMP_MIN);
}

/* ZCL 5.2.2.3.20: the values stay and are saved, with either capability */
ZTEST(zzb_zcl_color, test_stop_move_step)
{
	uint16_t x;

	move_to_color(40, 0x2000, 0x2000, 10);
	k_sleep(K_MSEC(350));
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STOP_MOVE_STEP, NULL, 0);
	x = color40.current_x;
	zassert_between_inclusive(x, 0x4000, 0x5000);
	zassert_equal(color40.remaining_time, 0);
	zassert_false(timer_armed());
	k_sleep(K_MSEC(200));
	zassert_equal(color40.current_x, x);
	restart();
	zassert_equal(color40.current_x, x);

	move_to_temperature(41, 450, 10);
	k_sleep(K_MSEC(350));
	send(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STOP_MOVE_STEP, NULL, 0);
	zassert_between_inclusive(color41.color_temperature, 300, 330);
	restart();
	zassert_between_inclusive(color41.color_temperature, 300, 330);
}

/* ZCL 5.2.2.2.1.10: while off, every command runs only with ExecuteIfOff */
ZTEST(zzb_zcl_color, test_execute_if_off)
{
	set_on_off(40, false);
	move_to_color(40, 0x2000, 0x2000, 0);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);

	/* OptionsMask selects the bits of OptionsOverride (ZCL 5.2.2.3.3) */
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_COLOR,
	     (uint8_t[]){0x00, 0x20, 0x00, 0x20, 0x0a, 0x00, 0x01, 0x01}, 8);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_XY);
	zassert_equal(color40.remaining_time, 10);
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STOP_MOVE_STEP, NULL, 0);
	zassert_equal(color40.remaining_time, 10);

	set_u8(40, ZZB_ZCL_COLOR_OPTIONS, 0x01);
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_STOP_MOVE_STEP, NULL, 0);
	zassert_equal(color40.remaining_time, 0);
	move_to_temperature(40, 300, 0);
	zassert_equal(color40.color_temperature, 300);
	send(40, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE,
	     (uint8_t[]){0x90, 0x01, 0x00, 0x00, 0x01, 0x00}, 6);
	zassert_equal(color40.color_temperature, 300);

	set_u8(40, ZZB_ZCL_COLOR_OPTIONS, 0x00);
	set_on_off(40, true);
	move_to_temperature(40, 400, 0);
	zassert_equal(color40.color_temperature, 400);
}

/*
 * ZCL 5.2.2.5: Store Scene of the Extended Color Light keeps the sets of On/Off, Level Control and
 * Color Control in 24 bytes; the attributes of hue, saturation and color loop are 0
 */
ZTEST(zzb_zcl_color, test_scene_store)
{
	static const uint8_t view_rsp[] = {0x19, 0x90, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
					   0x00, 0x06, 0x00, 0x01, 0x01, 0x08, 0x00, 0x01, 0xfe,
					   0x00, 0x03, 0x0d, 0x00, 0x20, 0x00, 0x30, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x00};

	move_to_color(40, 0x2000, 0x3000, 0);
	zassert_equal(zzb_zcl_scene_store(test_node(), 40, 0x0000, 1), ZZB_ZCL_SUCCESS);
	receive_ep(40, ZZB_ZCL_SCENES, (uint8_t[]){0x01, 0x90, 0x01, 0x00, 0x00, 0x01}, 6);
	check_frame(0, 40, ZZB_ZCL_SCENES, view_rsp, sizeof(view_rsp));
}

/* Section 16: a recalled scene moves X and Y in XY mode over its transition time */
ZTEST(zzb_zcl_color, test_scene_recall_xy)
{
	move_to_color(40, 0x2000, 0x3000, 0);
	zassert_equal(zzb_zcl_scene_store(test_node(), 40, 0x0000, 1), ZZB_ZCL_SUCCESS);

	/* The transition of a command sets SceneValid FALSE once, at its start */
	reset_fakes();
	move_to_color(40, 0x4000, 0x1000, 5);
	zassert_false(scenes40.scene_valid);
	k_sleep(K_MSEC(600));
	zassert_equal(color40.current_x, 0x4000);
	zassert_equal(notified(40, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_SCENE_VALID), 1);

	/* The transition of a recall leaves it TRUE */
	reset_fakes();
	send(40, ZZB_ZCL_SCENES, 0x05, (uint8_t[]){0x00, 0x00, 0x01, 0x05, 0x00}, 5);
	zassert_true(scenes40.scene_valid);
	zassert_equal(color40.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color40.current_x, 0x2000);
	zassert_equal(color40.current_y, 0x3000);
	zassert_true(scenes40.scene_valid);
	zassert_equal(notified(40, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_SCENE_VALID), 1);

	/* The color temperature is stored as XY, and the recall sets XY mode */
	move_to_temperature(40, 370, 0);
	zassert_equal(zzb_zcl_scene_store(test_node(), 40, 0x0000, 2), ZZB_ZCL_SUCCESS);
	move_to_temperature(40, 500, 0);
	zassert_equal(zzb_zcl_scene_recall(test_node(), 40, 0x0000, 2, 0), ZZB_ZCL_SUCCESS);
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_XY);
	zassert_equal(color40.current_x, 0x7587);
	zassert_equal(color40.current_y, 0x691e);
}

/* Section 16: without the XY capability a recall moves the color temperature in its range */
ZTEST(zzb_zcl_color, test_scene_recall_temperature)
{
	const uint32_t set[] = {0x2000, 0x3000, 0, 0, 0, 0, 0, 600};

	zzb_zcl_color_def.recall(test_node(), 41, &color41, set, 2, 0);
	zassert_equal(color41.color_temperature, 0x00fa);

	zzb_zcl_color_def.recall(test_node(), 41, &color41, set, ARRAY_SIZE(set), 5);
	zassert_equal(color41.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color41.color_temperature, COLOR_TEMP_MAX);
	zassert_equal(color41.current_x, 0x616b);
}

/*
 * ZCL Table 5.9: StartUpColorTemperatureMireds sets the color temperature and its mode at the
 * start, without zcl_cb. X and Y follow by Kim et al. (2002): 200 mireds (5000 K) x 0.344992
 * y 0.351587, computed in double precision.
 */
ZTEST(zzb_zcl_color, test_start_up)
{
	set_u16(41, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, 300);
	move_to_temperature(41, 400, 0);
	reset_fakes();
	restart();
	zassert_equal(color41.color_temperature, 300);
	zassert_equal(zcl_cb_calls.count, 0);

	/* Within the physical range */
	set_u16(41, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, 600);
	restart();
	zassert_equal(color41.color_temperature, COLOR_TEMP_MAX);

	/* 0xffff keeps the saved temperature */
	set_u16(41, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, 0xffff);
	move_to_temperature(41, 350, 0);
	restart();
	zassert_equal(color41.color_temperature, 350);

	/* From XY mode */
	move_to_color(40, 0x2000, 0x3000, 0);
	set_u16(40, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, 200);
	restart();
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.enhanced_color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.color_temperature, 200);
	zassert_equal(color40.current_x, 0x5851);
	zassert_equal(color40.current_y, 0x5a02);

	/* Without records StartUpColorTemperatureMireds is the default of the application */
	color41.start_up_default = 0x0100;
	reset_node();
	restart();
	zassert_equal(color41.start_up_color_temperature, 0x0100);
	zassert_equal(color41.color_temperature, 0x0100);
}

static void move_to_level(uint8_t ep, uint8_t level, uint16_t time)
{
	const uint8_t payload[] = {level, time & 0xff, time >> 8};

	send(ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL, payload, sizeof(payload));
}

static void set_level_options(uint8_t ep, uint8_t options)
{
	zassert_ok(
		zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_OPTIONS, &options));
}

/*
 * ZCL 5.2.2.1.1: with CoupleColorTempToLevel, level 254 gives CoupleColorTempToLevelMinMireds,
 * 1 ColorTempPhysicalMaxMireds and the levels between a linear value
 */
ZTEST(zzb_zcl_color, test_couple_to_level)
{
	set_level_options(41, 0x02);
	move_to_level(41, 1, 0);
	zassert_equal(color41.color_temperature, COLOR_TEMP_MAX);
	move_to_level(41, 128, 0);
	zassert_equal(color41.color_temperature,
		      COLOR_TEMP_MAX - (COLOR_TEMP_MAX - COLOR_COUPLE_MIN) * 127 / 253);

	/* Also during a transition of the level */
	reset_fakes();
	move_to_level(41, 254, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(color41.color_temperature, COLOR_COUPLE_MIN);
	zassert_between_inclusive(notified(41, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_TEMPERATURE), 4, 6);

	/* Not without the bit, and not in XY mode */
	set_level_options(41, 0x00);
	move_to_level(41, 1, 0);
	zassert_equal(color41.color_temperature, COLOR_COUPLE_MIN);
	set_level_options(40, 0x02);
	move_to_color(40, 0x2000, 0x3000, 0);
	move_to_level(40, 1, 0);
	zassert_equal(color40.color_temperature, 0x00fa);

	/* An endpoint without Color Control */
	set_level_options(10, 0x02);
	move_to_level(10, 100, 0);
	zassert_equal(level10.current_level, 100);
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
	zassert_equal(color40.current_x, 0x616a);
	zassert_equal(color40.current_y, 0x6072);
	zassert_equal(color40.color_temperature, 0x00fa);
	zassert_equal(color40.options, 0);
	zassert_equal(color40.start_up_color_temperature, 0xffff);
	check_zcl_cb(40, ZZB_ZCL_COLOR,
		     (uint16_t[]){ZZB_ZCL_COLOR_MODE, ZZB_ZCL_COLOR_ENHANCED_MODE,
				  ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_COLOR_CURRENT_Y,
				  ZZB_ZCL_COLOR_OPTIONS, ZZB_ZCL_COLOR_START_UP_TEMPERATURE},
		     6);

	restart();
	zassert_equal(color40.color_mode, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	zassert_equal(color40.current_x, 0x616a);
}

/* zzb_zcl_color_move_to_temperature() sends Move to Color Temperature without options */
ZTEST(zzb_zcl_color, test_send_move_to_temperature)
{
	uint8_t next_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_color_move_to_temperature(test_node(), CLIENT_EP, 0x0190, 0x0102));
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_COLOR);
	zassert_equal(sent[0].len, 7);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x11, next_tsn, 0x0a, 0x90, 0x01, 0x02, 0x01}),
			  7);
}

ZTEST_SUITE(zzb_zcl_color, NULL, setup, before, after, NULL);
