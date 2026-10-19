/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/alarms.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

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

/* Enters alarm code of cluster 0x0001 into the table without sending Alarm or calling zcl_cb */
static void alarm(uint8_t ep, uint8_t code)
{
	zzb_zcl_alarm_raise(test_node(), ep, 0x0001, code, false, false);
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

/* ZCL 3.11.2.5.1: an alarm of the application goes as Alarm to the bound destinations */
ZTEST(zzb_zcl_alarms, test_alarm_command)
{
	uint8_t tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_alarm(test_node(), 20, 0x0001, 0x3a));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(sent[0].req.src_ep, 20);
	zassert_equal(sent[0].req.profile, 0x0104);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_ALARMS);
	zassert_equal(sent[0].req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(sent[0].len, 6);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x19, tsn, 0x00, 0x3a, 0x01, 0x00}), 6);
	zassert_equal(alarms20.alarm_count, 1);

	/* Without a binding the alarm stays in the table */
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOENT;
	zassert_ok(zzb_zcl_alarm(test_node(), 20, 0x0001, 0x3b));
	zassert_equal(alarms20.alarm_count, 2);
}

/* ZCL 3.11.1: an alarm that its mask disables enters the table without Alarm */
ZTEST(zzb_zcl_alarms, test_masked_alarm)
{
	zzb_zcl_alarm_raise(test_node(), 20, 0x0001, 0x10, false, true);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	zassert_equal(alarms20.alarm_count, 1);
	zassert_equal(zcl_cb_calls.count, 1);
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

/* ZCL 3.11.2.4.1: Reset Alarm calls the alarm_reset hook of the server of its cluster */
ZTEST(zzb_zcl_alarms, test_reset_alarm)
{
	command(20, ZZB_ZCL_ALARMS_RESET_ALARM, (uint8_t[]){0x05, 0x02, 0x0f}, 3, ZZB_ZCL_SUCCESS);
	zassert_equal(alarm_reset_calls.count, 1);
	zassert_equal(alarm_reset_calls.ep, 20);
	zassert_equal_ptr(alarm_reset_calls.values, &values20);
	zassert_equal(alarm_reset_calls.code, 0x05);

	/* A cluster that the endpoint does not have, and a truncated command */
	command(21, ZZB_ZCL_ALARMS_RESET_ALARM, (uint8_t[]){0x05, 0x02, 0x0f}, 3, ZZB_ZCL_SUCCESS);
	command(20, ZZB_ZCL_ALARMS_RESET_ALARM, (uint8_t[]){0x05, 0x02}, 2,
		ZZB_ZCL_MALFORMED_COMMAND);
	zassert_equal(alarm_reset_calls.count, 1);
}

/* ZCL 3.11.2.4.2: Reset All Alarms calls the hook of every server on the endpoint with 0xff */
ZTEST(zzb_zcl_alarms, test_reset_all_alarms)
{
	command(20, ZZB_ZCL_ALARMS_RESET_ALL_ALARMS, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(alarm_reset_calls.count, 1);
	zassert_equal(alarm_reset_calls.ep, 20);
	zassert_equal(alarm_reset_calls.code, 0xff);

	command(21, ZZB_ZCL_ALARMS_RESET_ALL_ALARMS, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(alarm_reset_calls.count, 1);
}

/* The table survives a reboot, and new alarms follow the loaded ones; loading calls no zcl_cb */
ZTEST(zzb_zcl_alarms, test_table_survives_a_reboot)
{
	alarm(20, 0x10);
	alarm(21, 0x20);
	alarm(20, 0x11);
	get_alarm(20, 0x10);
	command(21, ZZB_ZCL_ALARMS_RESET_ALARM_LOG, NULL, 0, ZZB_ZCL_SUCCESS);
	zcl_cb_calls.count = 0;
	zassert_ok(reboot());

	zassert_equal(alarms20.alarm_count, 1);
	zassert_equal(alarms21.alarm_count, 0);
	zassert_equal(zcl_cb_calls.count, 0);
	alarm(20, 0x12);
	get_alarm(20, 0x11);
	get_alarm(20, 0x12);

	alarm(20, 0x13);
	reset_node();
	zassert_ok(reboot());
	zassert_equal(alarms20.alarm_count, 0);
}

ZTEST(zzb_zcl_alarms, test_bad_records)
{
	/* Sequence number 5, endpoint 20, alarm code 0x10 of cluster 0x0001 */
	uint8_t rec[] = {2, 5, 0, 0, 0, 20, 0x10, 0x01, 0x00};

	zassert_ok(settings_save_one("zzb/0/zcl/alarm/0", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/zcl/alarm/0", rec, sizeof(rec) - 1));
	zassert_equal(reboot(), -EINVAL);

	/* A record of an endpoint without an Alarms server, or of an unknown one, is ignored */
	rec[5] = 1;
	zassert_ok(settings_save_one("zzb/0/zcl/alarm/0", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_false(test_node()->zcl.alarms[0].in_use);
	rec[5] = 9;
	zassert_ok(settings_save_one("zzb/0/zcl/alarm/0", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_false(test_node()->zcl.alarms[0].in_use);

	rec[5] = 20;
	zassert_ok(settings_save_one("zzb/0/zcl/alarm/0", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_equal(alarms20.alarm_count, 1);
	get_alarm(20, 0x10);
}

ZTEST_SUITE(zzb_zcl_alarms, NULL, setup, before, after, NULL);
