/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts with the defaults, without records */
static void before(void *fixture)
{
	level10.start_up_default = 0xff;
	zassert_ok(reboot());
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the Level Control command id with the payload to ep and checks its Default Response */
static void command(uint8_t ep, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[16] = {0x01, tsn, id};

	memcpy(frame + 3, payload, len);
	receive_ep(ep, ZZB_ZCL_LEVEL, frame, 3 + len);
	check_frame(frames, ep, ZZB_ZCL_LEVEL, rsp, sizeof(rsp));
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

static void move_to_level(uint8_t ep, uint8_t level, uint16_t time)
{
	const uint8_t payload[] = {level, time & 0xff, time >> 8};

	send(ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL, payload, sizeof(payload));
}

static void set_u8(uint8_t ep, uint16_t attr, uint8_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_LEVEL, attr, &value));
}

static void set_u16(uint8_t ep, uint16_t attr, uint16_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_LEVEL, attr, &value));
}

static void set_on_off(uint8_t ep, uint16_t attr, uint16_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ep, ZZB_ZCL_ON_OFF, attr, &value));
}

static bool timer_armed(void)
{
	return k_work_delayable_busy_get(&test_node()->zcl.level_timer) != 0;
}

/* The number of zcl_cb calls for attribute attr of Level Control on ep */
static int notified(uint8_t ep, uint16_t attr)
{
	int n = 0;

	for (int i = 0; i < zcl_cb_calls.count; i++) {
		n += zcl_cb_calls.ep[i] == ep && zcl_cb_calls.cluster[i] == ZZB_ZCL_LEVEL &&
		     zcl_cb_calls.attr[i] == attr;
	}

	return n;
}

/* The index of the first zcl_cb call for attribute attr of cluster on ep, or -1 */
static int first_call(uint8_t ep, uint16_t cluster, uint16_t attr)
{
	for (int i = 0; i < zcl_cb_calls.count; i++) {
		if (zcl_cb_calls.ep[i] == ep && zcl_cb_calls.cluster[i] == cluster &&
		    zcl_cb_calls.attr[i] == attr) {
			return i;
		}
	}

	return -1;
}

/* Sends the On/Off command id to endpoint 11 */
static void on_off_command(uint8_t id)
{
	send(11, ZZB_ZCL_ON_OFF, id, NULL, 0);
}

/* ZCL 3.10.2.2, 3.19.2.2: the defaults, CurrentLevel read-only, the writable attributes saved */
ZTEST(zzb_zcl_level, test_attributes)
{
	static const uint8_t read[] = {0x00, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00,
				       0x0f, 0x00, 0x10, 0x00, 0x11, 0x00, 0x12,
				       0x00, 0x13, 0x00, 0x00, 0x40, 0xfd, 0xff};
	static const uint8_t read_rsp[] = {
		0x18, 0x80, 0x01, 0x00, 0x00, 0x00, 0x20, 0xfe, 0x01, 0x00, 0x00, 0x21, 0x00, 0x00,
		0x0f, 0x00, 0x00, 0x18, 0x00, 0x10, 0x00, 0x00, 0x21, 0x00, 0x00, 0x11, 0x00, 0x00,
		0x20, 0xff, 0x12, 0x00, 0x00, 0x21, 0xff, 0xff, 0x13, 0x00, 0x00, 0x21, 0xff, 0xff,
		0x00, 0x40, 0x00, 0x20, 0xff, 0xfd, 0xff, 0x00, 0x21, 0x02, 0x00};
	static const uint8_t write[] = {0x00, 0x81, 0x02, 0x00, 0x00, 0x20, 0x10, 0x0f, 0x00,
					0x18, 0x01, 0x10, 0x00, 0x21, 0x05, 0x00, 0x11, 0x00,
					0x20, 0x80, 0x12, 0x00, 0x21, 0x0a, 0x00, 0x13, 0x00,
					0x21, 0x14, 0x00, 0x00, 0x40, 0x20, 0x00};
	static const uint8_t write_rsp[] = {0x18, 0x81, 0x04, 0x88, 0x00, 0x00};
	static const uint8_t reread[] = {0x00, 0x82, 0x00, 0x0f, 0x00, 0x10, 0x00, 0x11,
					 0x00, 0x12, 0x00, 0x13, 0x00, 0x00, 0x40};
	static const uint8_t reread_rsp[] = {0x18, 0x82, 0x01, 0x0f, 0x00, 0x00, 0x18, 0x01, 0x10,
					     0x00, 0x00, 0x21, 0x05, 0x00, 0x11, 0x00, 0x00, 0x20,
					     0x80, 0x12, 0x00, 0x00, 0x21, 0x0a, 0x00, 0x13, 0x00,
					     0x00, 0x21, 0x14, 0x00, 0x00, 0x40, 0x00, 0x20, 0x00};

	receive_ep(10, ZZB_ZCL_LEVEL, read, sizeof(read));
	check_frame(0, 10, ZZB_ZCL_LEVEL, read_rsp, sizeof(read_rsp));
	receive_ep(10, ZZB_ZCL_LEVEL, write, sizeof(write));
	check_frame(1, 10, ZZB_ZCL_LEVEL, write_rsp, sizeof(write_rsp));

	zassert_ok(reboot());
	receive_ep(10, ZZB_ZCL_LEVEL, reread, sizeof(reread));
	check_frame(2, 10, ZZB_ZCL_LEVEL, reread_rsp, sizeof(reread_rsp));
}

/* ZCL 3.10.2.3.1: the level is limited to 1 to 254 (ZCL 3.19.2.2.1) and saved */
ZTEST(zzb_zcl_level, test_move_to_level)
{
	move_to_level(10, 0x80, 0);
	zassert_equal(level10.current_level, 0x80);
	zassert_equal(zcl_cb_calls.count, 1);
	zassert_equal(zcl_cb_calls.ep[0], 10);
	zassert_equal(zcl_cb_calls.cluster[0], ZZB_ZCL_LEVEL);
	zassert_equal(zcl_cb_calls.attr[0], ZZB_ZCL_LEVEL_CURRENT_LEVEL);

	zassert_ok(reboot());
	zassert_equal(level10.current_level, 0x80);

	move_to_level(10, 0x00, 0);
	zassert_equal(level10.current_level, 1);
	move_to_level(10, 0xff, 0);
	zassert_equal(level10.current_level, 254);

	command(10, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL, (uint8_t[]){0x80, 0x00}, 2,
		ZZB_ZCL_MALFORMED_COMMAND);
	command(10, 0x08, NULL, 0, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(level10.current_level, 254);
}

/* Section 16: a transition steps CurrentLevel every 100 ms and saves only its end */
ZTEST(zzb_zcl_level, test_transition)
{
	move_to_level(10, 100, 0);
	move_to_level(10, 200, 10);
	zassert_equal(level10.current_level, 100);
	zassert_equal(level10.remaining_time, 10);
	zassert_true(timer_armed());

	k_sleep(K_MSEC(550));
	zassert_between_inclusive(level10.current_level, 140, 155);
	zassert_between_inclusive(level10.remaining_time, 4, 6);

	/* The record holds the start value */
	zassert_ok(reboot());
	zassert_equal(level10.current_level, 100);
	zassert_equal(level10.remaining_time, 0);
	zassert_false(timer_armed());

	reset_fakes();
	move_to_level(10, 200, 10);
	k_sleep(K_MSEC(1200));
	zassert_equal(level10.current_level, 200);
	zassert_equal(level10.remaining_time, 0);
	zassert_between_inclusive(notified(10, ZZB_ZCL_LEVEL_CURRENT_LEVEL), 9, 11);
	zassert_false(timer_armed());
	zassert_ok(reboot());
	zassert_equal(level10.current_level, 200);
}

/* ZCL 3.10.2.3.1: transition time 0xffff takes OnOffTransitionTime */
ZTEST(zzb_zcl_level, test_on_off_transition_time)
{
	set_u16(10, ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, 5);
	move_to_level(10, 100, 0xffff);
	zassert_equal(level10.current_level, 254);
	zassert_equal(level10.remaining_time, 5);
	k_sleep(K_MSEC(700));
	zassert_equal(level10.current_level, 100);
}

/* Section 16: a value set by the application ends the transition, a new command replaces it */
ZTEST(zzb_zcl_level, test_transition_ends)
{
	move_to_level(10, 100, 10);
	k_sleep(K_MSEC(250));
	set_u8(10, ZZB_ZCL_LEVEL_CURRENT_LEVEL, 30);
	k_sleep(K_MSEC(150));
	zassert_equal(level10.current_level, 30);
	zassert_equal(level10.remaining_time, 0);
	zassert_false(timer_armed());

	move_to_level(10, 100, 10);
	k_sleep(K_MSEC(250));
	move_to_level(10, 200, 0);
	zassert_equal(level10.current_level, 200);
	zassert_equal(level10.remaining_time, 0);
	zassert_false(timer_armed());
}

/* Section 16: the steps run the reporting check */
ZTEST(zzb_zcl_level, test_transition_reported)
{
	static const uint8_t configure[] = {0x00, 0x83, 0x06, 0x00, 0x00, 0x00,
					    0x20, 0x01, 0x00, 0x00, 0x00, 0x01};

	receive_ep(10, ZZB_ZCL_LEVEL, configure, sizeof(configure));
	check_frame(0, 10, ZZB_ZCL_LEVEL, (uint8_t[]){0x18, 0x83, 0x07, 0x00}, 4);
	move_to_level(10, 200, 5);
	k_sleep(K_MSEC(1100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	zassert_equal(sent[1].req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_mem_equal(sent[1].data + 2, ((uint8_t[]){0x0a, 0x00, 0x00, 0x20, 200}), 5);
}

/* ZCL 3.10.2.3.2: Move goes to the limit at the rate */
ZTEST(zzb_zcl_level, test_move)
{
	move_to_level(10, 154, 0);
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x00, 100}, 2);
	zassert_equal(level10.remaining_time, 10);
	k_sleep(K_MSEC(550));
	zassert_between_inclusive(level10.current_level, 195, 209);
	k_sleep(K_MSEC(600));
	zassert_equal(level10.current_level, 254);
	zassert_false(timer_armed());

	/* Rate 0xff moves at once */
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x01, 0xff}, 2);
	zassert_equal(level10.current_level, 1);

	command(10, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x00, 0x00}, 2, ZZB_ZCL_INVALID_FIELD);
	command(10, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x02, 0x10}, 2, ZZB_ZCL_INVALID_FIELD);
	command(10, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x00}, 1, ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(level10.current_level, 1);
}

/* ZCL 3.10.2.3.3: a step cut at a limit takes the same part of the transition time */
ZTEST(zzb_zcl_level, test_step)
{
	move_to_level(10, 100, 0);
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STEP, (uint8_t[]){0x00, 50, 0x0a, 0x00}, 4);
	zassert_equal(level10.remaining_time, 10);
	k_sleep(K_MSEC(1100));
	zassert_equal(level10.current_level, 150);

	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STEP, (uint8_t[]){0x00, 208, 0x0a, 0x00}, 4);
	zassert_equal(level10.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(level10.current_level, 254);

	/* Transition time 0xffff steps at once */
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STEP, (uint8_t[]){0x01, 4, 0xff, 0xff}, 4);
	zassert_equal(level10.current_level, 250);

	command(10, ZZB_ZCL_LEVEL_STEP, (uint8_t[]){0x02, 1, 0x00, 0x00}, 4, ZZB_ZCL_INVALID_FIELD);
	command(10, ZZB_ZCL_LEVEL_STEP, (uint8_t[]){0x00, 1, 0x00}, 3, ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(level10.current_level, 250);
}

/* ZCL 3.10.2.3.4: Stop leaves the level, which is saved */
ZTEST(zzb_zcl_level, test_stop)
{
	uint8_t level;

	move_to_level(10, 100, 10);
	k_sleep(K_MSEC(350));
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STOP, NULL, 0);
	level = level10.current_level;
	zassert_between_inclusive(level, 190, 225);
	zassert_equal(level10.remaining_time, 0);
	zassert_false(timer_armed());

	k_sleep(K_MSEC(200));
	zassert_equal(level10.current_level, level);
	zassert_ok(reboot());
	zassert_equal(level10.current_level, level);
}

/* ZCL 3.10.2.2.8.1: while off, a command without On/Off runs only with ExecuteIfOff */
ZTEST(zzb_zcl_level, test_execute_if_off)
{
	move_to_level(11, 100, 0);
	zassert_equal(level11.current_level, 254);

	/* OptionsMask selects the bits of OptionsOverride (ZCL 3.10.2.3.1.2) */
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL, (uint8_t[]){100, 0, 0, 0x01, 0x01}, 5);
	zassert_equal(level11.current_level, 100);

	set_u8(11, ZZB_ZCL_LEVEL_OPTIONS, 0x01);
	move_to_level(11, 120, 0);
	zassert_equal(level11.current_level, 120);
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE, (uint8_t[]){0x00, 0xff, 0x01, 0x00}, 4);
	zassert_equal(level11.current_level, 120);

	set_u8(11, ZZB_ZCL_LEVEL_OPTIONS, 0x00);
	set_on_off(11, ZZB_ZCL_ON_OFF_ON_OFF, true);
	move_to_level(11, 140, 0);
	zassert_equal(level11.current_level, 140);
}

/* ZCL 3.10.2.3.6: with On/Off the endpoint turns on before a move above 1 and off at 1 */
ZTEST(zzb_zcl_level, test_with_on_off)
{
	/* ZHA sends 3 octets; the On rules apply to OffWaitTime (ZCL 3.8.2.1.1) */
	set_on_off(11, ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 20);
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){200, 5, 0}, 3);
	zassert_true(on_off11.on_off);
	zassert_equal(on_off11.off_wait_time, 0);
	zassert_equal(level11.current_level, 254);
	k_sleep(K_MSEC(600));
	zassert_equal(level11.current_level, 200);

	/* Level 0 ends at 1, and OnOff becomes FALSE at the end with the Off rules */
	set_on_off(11, ZZB_ZCL_ON_OFF_ON_TIME, 30);
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){0, 5, 0}, 3);
	k_sleep(K_MSEC(300));
	zassert_true(on_off11.on_off);
	zassert_between_inclusive(level11.current_level, 60, 141);
	k_sleep(K_MSEC(300));
	zassert_equal(level11.current_level, 1);
	zassert_false(on_off11.on_off);
	zassert_equal(on_off11.on_time, 0);

	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STEP_ON_OFF, (uint8_t[]){0x00, 10, 0xff, 0xff}, 4);
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 11);
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_ON_OFF, (uint8_t[]){0x01, 0xff}, 2);
	zassert_false(on_off11.on_off);
	zassert_equal(level11.current_level, 1);

	/* Without an On/Off server only the level moves */
	send(10, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){0, 0, 0}, 3);
	zassert_equal(level10.current_level, 1);
}

/* A command of the cluster cancels the change of OnOff at the end */
ZTEST(zzb_zcl_level, test_with_on_off_stopped)
{
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){254, 0, 0}, 3);
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){0, 5, 0}, 3);
	k_sleep(K_MSEC(200));
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_STOP_ON_OFF, NULL, 0);
	k_sleep(K_MSEC(500));
	zassert_true(on_off11.on_off);
	zassert_true(level11.current_level > 1);
}

/* ZCL Table 3-55: On while off moves the level from 1 back to the level before */
ZTEST(zzb_zcl_level, test_on)
{
	set_u8(11, ZZB_ZCL_LEVEL_CURRENT_LEVEL, 200);
	set_u16(11, ZZB_ZCL_LEVEL_ON_TRANSITION_TIME, 10);
	on_off_command(0x01);
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 1);
	zassert_true(first_call(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_CURRENT_LEVEL) <
		     first_call(11, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF));

	/* OnTransitionTime is the time from 1 to 254 */
	zassert_equal(level11.remaining_time, 7);
	k_sleep(K_MSEC(400));
	zassert_between_inclusive(level11.current_level, 80, 120);
	k_sleep(K_MSEC(500));
	zassert_equal(level11.current_level, 200);

	/* On while on leaves the level */
	on_off_command(0x01);
	zassert_equal(level11.current_level, 200);
	zassert_false(timer_armed());
}

/* ZCL Table 3-55: Toggle while off moves to OnLevel over OnOffTransitionTime */
ZTEST(zzb_zcl_level, test_on_level)
{
	set_u8(11, ZZB_ZCL_LEVEL_CURRENT_LEVEL, 200);
	set_u8(11, ZZB_ZCL_LEVEL_ON_LEVEL, 100);
	set_u16(11, ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, 5);
	on_off_command(0x02);
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 1);
	zassert_equal(level11.remaining_time, 5);
	k_sleep(K_MSEC(600));
	zassert_equal(level11.current_level, 100);

	/* Transition time 0 writes the target at once, without the step to 1 */
	set_on_off(11, ZZB_ZCL_ON_OFF_ON_OFF, false);
	set_u16(11, ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, 0);
	set_u8(11, ZZB_ZCL_LEVEL_ON_LEVEL, 150);
	reset_fakes();
	on_off_command(0x01);
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 150);
	zassert_equal(notified(11, ZZB_ZCL_LEVEL_CURRENT_LEVEL), 1);
}

/* ZCL Table 3-55: Off fades to 1 and turns off at the end; with OnLevel 0xff the level returns */
ZTEST(zzb_zcl_level, test_off)
{
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){200, 0, 0}, 3);

	/* At once only OnOff changes */
	reset_fakes();
	on_off_command(0x00);
	zassert_false(on_off11.on_off);
	zassert_equal(level11.current_level, 200);
	zassert_equal(notified(11, ZZB_ZCL_LEVEL_CURRENT_LEVEL), 0);
	on_off_command(0x01);

	set_on_off(11, ZZB_ZCL_ON_OFF_ON_TIME, 30);
	set_u16(11, ZZB_ZCL_LEVEL_OFF_TRANSITION_TIME, 10);
	reset_fakes();

	/* OnTime becomes 0 at once, OnOff at the end */
	on_off_command(0x00);
	zassert_true(on_off11.on_off);
	zassert_equal(on_off11.on_time, 0);
	zassert_equal(level11.remaining_time, 7);
	k_sleep(K_MSEC(400));
	zassert_true(on_off11.on_off);
	zassert_between_inclusive(level11.current_level, 80, 120);
	k_sleep(K_MSEC(500));
	zassert_false(on_off11.on_off);
	zassert_equal(level11.current_level, 200);
	zassert_equal(zcl_cb_calls.attr[zcl_cb_calls.count - 2], ZZB_ZCL_ON_OFF_ON_OFF);
	zassert_equal(zcl_cb_calls.cluster[zcl_cb_calls.count - 1], ZZB_ZCL_LEVEL);
	zassert_equal(zcl_cb_calls.attr[zcl_cb_calls.count - 1], ZZB_ZCL_LEVEL_CURRENT_LEVEL);

	/* Off while off leaves the level */
	on_off_command(0x00);
	zassert_false(timer_armed());

	/* With OnLevel the level stays at 1 */
	set_u8(11, ZZB_ZCL_LEVEL_ON_LEVEL, 100);
	on_off_command(0x01);
	zassert_equal(level11.current_level, 100);
	on_off_command(0x02);
	zassert_equal(level11.remaining_time, 3);
	k_sleep(K_MSEC(400));
	zassert_false(on_off11.on_off);
	zassert_equal(level11.current_level, 1);
}

/* ZCL 3.10.2.1.1: during the fade of Off, On goes back to the stored level and Off is ignored */
ZTEST(zzb_zcl_level, test_off_interrupted)
{
	send(11, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, (uint8_t[]){200, 0, 0}, 3);
	set_u16(11, ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, 10);
	on_off_command(0x00);
	k_sleep(K_MSEC(300));
	on_off_command(0x00);
	zassert_between_inclusive(level11.remaining_time, 6, 8);

	on_off_command(0x02);
	zassert_true(on_off11.on_off);
	zassert_equal(level11.remaining_time, 10);
	k_sleep(K_MSEC(1100));
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 200);

	/* A Level Control command cancels the change of OnOff at the end */
	on_off_command(0x00);
	k_sleep(K_MSEC(300));
	move_to_level(11, 50, 0);
	k_sleep(K_MSEC(800));
	zassert_true(on_off11.on_off);
	zassert_equal(level11.current_level, 50);
}

ZTEST_SUITE(zzb_zcl_level, NULL, setup, before, after, NULL);
