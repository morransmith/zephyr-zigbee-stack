/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/level.h>

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

ZTEST_SUITE(zzb_zcl_level, NULL, setup, before, after, NULL);
