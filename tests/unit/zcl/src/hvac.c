/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/fan.h>

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

/*
 * ZCL Table 6-41: FanMode is 0x00 to 0x06, and FanModeSequence is read-only. The first command
 * writes FanMode 0x07 and FanModeSequence 0x01, the second FanMode 0x06.
 */
ZTEST(zzb_zcl_hvac, test_fan_mode_checks)
{
	static const uint8_t req1[] = {0x00, 0x40, 0x02, 0x00, 0x00, 0x30,
				       0x07, 0x01, 0x00, 0x30, 0x01};
	static const uint8_t rsp1[] = {0x18, 0x40, 0x04, 0x87, 0x00, 0x00, 0x88, 0x01, 0x00};
	static const uint8_t req2[] = {0x00, 0x41, 0x02, 0x00, 0x00, 0x30, 0x06};
	static const uint8_t rsp2[] = {0x18, 0x41, 0x04, 0x00};

	receive_ep(HVAC_EP, ZZB_ZCL_FAN, req1, sizeof(req1));
	check_frame(0, HVAC_EP, ZZB_ZCL_FAN, rsp1, sizeof(rsp1));
	zassert_equal(fan70.fan_mode, 0x05);
	zassert_equal(fan70.fan_mode_sequence, 0x02);

	receive_ep(HVAC_EP, ZZB_ZCL_FAN, req2, sizeof(req2));
	check_frame(1, HVAC_EP, ZZB_ZCL_FAN, rsp2, sizeof(rsp2));
	zassert_equal(fan70.fan_mode, 0x06);
}

/*
 * FanMode is Auto (0x05) without records, is saved and reportable, and the reset restores Auto.
 * The Configure Reporting has the intervals of ZHA 2.3.1, 5 s and 900 s.
 */
ZTEST(zzb_zcl_hvac, test_fan_mode_settings)
{
	static const uint8_t write[] = {0x00, 0x42, 0x02, 0x00, 0x00, 0x30, 0x01};
	static const uint8_t write_rsp[] = {0x18, 0x42, 0x04, 0x00};
	static const uint8_t report[] = {0x00, 0x43, 0x06, 0x00, 0x00, 0x00,
					 0x30, 0x05, 0x00, 0x84, 0x03};
	static const uint8_t report_rsp[] = {0x18, 0x43, 0x07, 0x00};
	static const uint16_t reset_attrs[] = {ZZB_ZCL_FAN_MODE};

	fan70.fan_mode = 0x00;
	zassert_ok(reboot());
	zassert_equal(fan70.fan_mode, 0x05);

	receive_ep(HVAC_EP, ZZB_ZCL_FAN, write, sizeof(write));
	check_frame(0, HVAC_EP, ZZB_ZCL_FAN, write_rsp, sizeof(write_rsp));
	zassert_ok(reboot());
	zassert_equal(fan70.fan_mode, 0x01);

	receive_ep(HVAC_EP, ZZB_ZCL_FAN, report, sizeof(report));
	check_frame(1, HVAC_EP, ZZB_ZCL_FAN, report_rsp, sizeof(report_rsp));

	reset_fakes();
	reset_node();
	zassert_equal(fan70.fan_mode, 0x05);
	check_zcl_cb(HVAC_EP, ZZB_ZCL_FAN, reset_attrs, ARRAY_SIZE(reset_attrs));
}

ZTEST_SUITE(zzb_zcl_hvac, NULL, setup, before, after, NULL);
