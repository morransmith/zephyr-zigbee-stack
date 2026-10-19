/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/identify.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	zassert_ok(reboot());
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static void check_identify_time(int i, uint16_t time)
{
	zassert_equal(identify.identify_time, time);
	zassert_true(zcl_cb_calls.count > i);
	zassert_equal(zcl_cb_calls.ep[i], 2);
	zassert_equal(zcl_cb_calls.cluster[i], ZZB_ZCL_IDENTIFY);
	zassert_equal(zcl_cb_calls.attr[i], ZZB_ZCL_IDENTIFY_TIME);
}

static bool timer_armed(void)
{
	return k_work_delayable_busy_get(&test_node()->zcl.identify_timer) != 0;
}

/* ZCL 3.5.2.2.1: IdentifyTime decrements every second until 0 */
ZTEST(zzb_zcl_identify, test_identify)
{
	static const uint8_t rsp[] = {0x18, 0x80, 0x0b, 0x00, 0x00};

	receive_ep(2, ZZB_ZCL_IDENTIFY, (uint8_t[]){0x01, 0x80, 0x00, 0x03, 0x00}, 5);
	check_frame(0, 2, ZZB_ZCL_IDENTIFY, rsp, sizeof(rsp));
	check_identify_time(0, 3);

	k_sleep(K_MSEC(1100));
	check_identify_time(1, 2);
	k_sleep(K_SECONDS(2));
	check_identify_time(3, 0);
	zassert_equal(zcl_cb_calls.count, 4);
	zassert_false(timer_armed());
}

/* A write starts the identification and the Identify command stops it */
ZTEST(zzb_zcl_identify, test_write_and_stop)
{
	static const uint8_t write[] = {0x00, 0x81, 0x02, 0x00, 0x00, 0x21, 0x0a, 0x00};

	receive_ep(2, ZZB_ZCL_IDENTIFY, write, sizeof(write));
	check_frame(0, 2, ZZB_ZCL_IDENTIFY, (uint8_t[]){0x18, 0x81, 0x04, 0x00}, 4);
	check_identify_time(0, 10);
	zassert_true(timer_armed());

	receive_ep(2, ZZB_ZCL_IDENTIFY, (uint8_t[]){0x11, 0x82, 0x00, 0x00, 0x00}, 5);
	check_identify_time(1, 0);
	zassert_false(timer_armed());
}

ZTEST(zzb_zcl_identify, test_read)
{
	static const uint8_t rsp[] = {0x18, 0x83, 0x01, 0x00, 0x00, 0x00, 0x21, 0x00,
				      0x00, 0xfd, 0xff, 0x00, 0x21, 0x02, 0x00};

	receive_ep(2, ZZB_ZCL_IDENTIFY, (uint8_t[]){0x00, 0x83, 0x00, 0x00, 0x00, 0xfd, 0xff}, 7);
	check_frame(0, 2, ZZB_ZCL_IDENTIFY, rsp, sizeof(rsp));
}

/* ZCL Table 2-12: the Identify Time field is missing */
ZTEST(zzb_zcl_identify, test_malformed)
{
	static const uint8_t rsp[] = {0x18, 0x84, 0x0b, 0x00, 0x80};

	receive_ep(2, ZZB_ZCL_IDENTIFY, (uint8_t[]){0x01, 0x84, 0x00, 0x03}, 4);
	check_frame(0, 2, ZZB_ZCL_IDENTIFY, rsp, sizeof(rsp));
	zassert_equal(identify.identify_time, 0);
}

ZTEST_SUITE(zzb_zcl_identify, NULL, setup, before, after, NULL);
