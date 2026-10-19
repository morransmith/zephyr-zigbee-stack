/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/alarms.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

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

/* Sends the Alarms command id with the payload to ep and checks its Default Response */
static void command(uint8_t ep, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[8] = {0x01, ++tsn, id};
	const uint8_t expected[] = {0x18, tsn, 0x0b, id, status};

	memcpy(frame + 3, payload, len);
	receive_ep(ep, ZZB_ZCL_ALARMS, frame, 3 + len);
	check_frame(frames, ep, ZZB_ZCL_ALARMS, expected, sizeof(expected));
}

/* Sends Get Alarm to ep and checks that its response has the payload rsp */
static void get_alarm_rsp(uint8_t ep, const uint8_t *rsp, size_t len)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	const uint8_t req[] = {0x01, ++tsn, ZZB_ZCL_ALARMS_GET_ALARM};
	uint8_t expected[16] = {0x19, tsn, ZZB_ZCL_ALARMS_GET_ALARM_RSP};

	memcpy(expected + 3, rsp, len);
	receive_ep(ep, ZZB_ZCL_ALARMS, req, sizeof(req));
	check_frame(frames, ep, ZZB_ZCL_ALARMS, expected, 3 + len);
}

/* ZCL 3.11.2.5.2: the alarm code of cluster 0x0001 and no time stamp */
static void get_alarm(uint8_t ep, uint8_t code)
{
	get_alarm_rsp(ep, (uint8_t[]){0x00, code, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff}, 8);
}

static void get_no_alarm(uint8_t ep)
{
	get_alarm_rsp(ep, (uint8_t[]){ZZB_ZCL_NOT_FOUND}, 1);
}

static void alarm(uint8_t ep, uint8_t code)
{
	zassert_ok(zzb_zcl_alarm(test_node(), ep, 0x0001, code));
}

/* ZCL 3.11.2.2.1: AlarmCount is a uint16; ClusterRevision 1 */
ZTEST(zzb_zcl_alarms, test_attributes)
{
	static const uint8_t req[] = {0x00, 0x90, 0x00, 0x00, 0x00, 0xfd, 0xff};
	static const uint8_t rsp[] = {0x18, 0x90, 0x01, 0x00, 0x00, 0x00, 0x21, 0x02,
				      0x00, 0xfd, 0xff, 0x00, 0x21, 0x01, 0x00};

	alarm(20, 0x10);
	alarm(20, 0x11);
	receive_ep(20, ZZB_ZCL_ALARMS, req, sizeof(req));
	check_frame(0, 20, ZZB_ZCL_ALARMS, rsp, sizeof(rsp));
}

/* ZCL 3.11.2.4.3: Get Alarm reports the earliest entry of the endpoint and removes it */
ZTEST(zzb_zcl_alarms, test_get_alarm)
{
	alarm(20, 0x10);
	alarm(21, 0x20);
	alarm(20, 0x11);
	zassert_equal(alarms20.alarm_count, 2);
	zassert_equal(alarms21.alarm_count, 1);
	zassert_equal(zcl_cb_calls.count, 0);

	get_alarm(20, 0x10);
	zassert_equal(alarms20.alarm_count, 1);
	zassert_equal(zcl_cb_calls.count, 1);
	zassert_equal(zcl_cb_calls.ep[0], 20);
	zassert_equal(zcl_cb_calls.cluster[0], ZZB_ZCL_ALARMS);
	zassert_equal(zcl_cb_calls.attr[0], ZZB_ZCL_ALARMS_ALARM_COUNT);
	get_alarm(20, 0x11);
	get_no_alarm(20);
	zassert_equal(alarms20.alarm_count, 0);
	zassert_equal(alarms21.alarm_count, 1);
}

/* Get Alarm Response also answers a broadcast */
ZTEST(zzb_zcl_alarms, test_get_alarm_broadcast)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = 0xfffd,
		.dst_ep = 20,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_ALARMS,
	};
	static const uint8_t req[] = {0x01, 0x91, 0x02};
	static const uint8_t rsp[] = {0x19, 0x91, 0x01, 0x00, 0x10, 0x01,
				      0x00, 0xff, 0xff, 0xff, 0xff};

	alarm(20, 0x10);
	receive_ind(&ind, req, sizeof(req));
	check_frame(0, 20, ZZB_ZCL_ALARMS, rsp, sizeof(rsp));
}

/* ZCL 3.11.2.3: a full table replaces its earliest entry, also of another endpoint */
ZTEST(zzb_zcl_alarms, test_full_table)
{
	alarm(21, 0x20);
	for (int i = 0; i < CONFIG_ZZB_ZCL_ALARM_COUNT; i++) {
		alarm(20, 0x10 + i);
	}

	zassert_equal(alarms20.alarm_count, CONFIG_ZZB_ZCL_ALARM_COUNT);
	zassert_equal(alarms21.alarm_count, 0);
	get_no_alarm(21);
	get_alarm(20, 0x10);

	alarm(21, 0x21);
	alarm(21, 0x22);
	zassert_equal(alarms20.alarm_count, 2);
	get_alarm(20, 0x12);
	get_alarm(21, 0x21);
}

/* ZCL 3.11.2.4.4: Reset Alarm Log clears the entries of the endpoint */
ZTEST(zzb_zcl_alarms, test_reset_alarm_log)
{
	alarm(20, 0x10);
	alarm(20, 0x11);
	alarm(21, 0x20);
	command(20, ZZB_ZCL_ALARMS_RESET_ALARM_LOG, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(alarms20.alarm_count, 0);
	zassert_equal(zcl_cb_calls.count, 1);
	get_no_alarm(20);
	get_alarm(21, 0x20);
}

ZTEST(zzb_zcl_alarms, test_alarm_without_server)
{
	zassert_equal(zzb_zcl_alarm(test_node(), 1, 0x0001, 0x10), -ENOENT);
	zassert_equal(zzb_zcl_alarm(test_node(), 9, 0x0001, 0x10), -ENOENT);
}

/* The reset of the node clears the table */
ZTEST(zzb_zcl_alarms, test_reset)
{
	alarm(20, 0x10);
	alarm(21, 0x20);
	reset_node();
	zassert_equal(alarms20.alarm_count, 0);
	zassert_equal(alarms21.alarm_count, 0);
	zassert_equal(zcl_cb_calls.count, 2);
	get_no_alarm(20);
	get_no_alarm(21);
}

ZTEST_SUITE(zzb_zcl_alarms, NULL, setup, before, after, NULL);
