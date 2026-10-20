/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/fan.h>
#include <zzb/zcl/pump.h>

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

/*
 * ZCL Tables 6-7 and 6-8: OperationMode is 0 to 3, ControlMode 0 to 3, 5 or 7; Power and
 * LifetimeEnergyConsumed are read-only. The first command writes OperationMode 4, ControlMode 4,
 * Power 1 and LifetimeEnergyConsumed 1, the second OperationMode 3 and ControlMode 6, the third
 * ControlMode 8, the fourth ControlMode 5.
 */
ZTEST(zzb_zcl_hvac, test_pump_checks)
{
	static const uint8_t req1[] = {0x00, 0x40, 0x02, 0x20, 0x00, 0x30, 0x04, 0x21,
				       0x00, 0x30, 0x04, 0x16, 0x00, 0x22, 0x01, 0x00,
				       0x00, 0x17, 0x00, 0x23, 0x01, 0x00, 0x00, 0x00};
	static const uint8_t rsp1[] = {0x18, 0x40, 0x04, 0x87, 0x20, 0x00, 0x87, 0x21,
				       0x00, 0x88, 0x16, 0x00, 0x88, 0x17, 0x00};
	static const uint8_t req2[] = {0x00, 0x41, 0x02, 0x20, 0x00, 0x30,
				       0x03, 0x21, 0x00, 0x30, 0x06};
	static const uint8_t rsp2[] = {0x18, 0x41, 0x04, 0x87, 0x21, 0x00};
	static const uint8_t req3[] = {0x00, 0x42, 0x02, 0x21, 0x00, 0x30, 0x08};
	static const uint8_t rsp3[] = {0x18, 0x42, 0x04, 0x87, 0x21, 0x00};
	static const uint8_t req4[] = {0x00, 0x43, 0x02, 0x21, 0x00, 0x30, 0x05};
	static const uint8_t rsp4[] = {0x18, 0x43, 0x04, 0x00};

	receive_ep(HVAC_EP, ZZB_ZCL_PUMP, req1, sizeof(req1));
	check_frame(0, HVAC_EP, ZZB_ZCL_PUMP, rsp1, sizeof(rsp1));
	zassert_equal(pump70.operation_mode, 0);
	zassert_equal(pump70.control_mode, 0);

	receive_ep(HVAC_EP, ZZB_ZCL_PUMP, req2, sizeof(req2));
	check_frame(1, HVAC_EP, ZZB_ZCL_PUMP, rsp2, sizeof(rsp2));
	zassert_equal(pump70.operation_mode, 3);
	zassert_equal(pump70.control_mode, 0);

	receive_ep(HVAC_EP, ZZB_ZCL_PUMP, req3, sizeof(req3));
	check_frame(2, HVAC_EP, ZZB_ZCL_PUMP, rsp3, sizeof(rsp3));
	receive_ep(HVAC_EP, ZZB_ZCL_PUMP, req4, sizeof(req4));
	check_frame(3, HVAC_EP, ZZB_ZCL_PUMP, rsp4, sizeof(rsp4));
	zassert_equal(pump70.control_mode, 5);
}

/*
 * OperationMode, ControlMode and LifetimeRunningHours are 0 without records, are saved, and the
 * reset sets them to 0. LifetimeEnergyConsumed, which the application sets, is saved and kept by
 * the reset. The write is OperationMode 1, ControlMode 7 and LifetimeRunningHours 0x123456.
 */
ZTEST(zzb_zcl_hvac, test_pump_settings)
{
	static const uint8_t req[] = {0x00, 0x44, 0x02, 0x20, 0x00, 0x30, 0x01, 0x21, 0x00,
				      0x30, 0x07, 0x15, 0x00, 0x22, 0x56, 0x34, 0x12};
	static const uint8_t rsp[] = {0x18, 0x44, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {0x0015, 0x0020, 0x0021};
	uint32_t energy = 0x12345678;

	pump70.operation_mode = 2;
	pump70.control_mode = 1;
	pump70.lifetime_running_hours = 5;
	zassert_ok(reboot());
	zassert_equal(pump70.operation_mode, 0);
	zassert_equal(pump70.control_mode, 0);
	zassert_equal(pump70.lifetime_running_hours, 0);

	receive_ep(HVAC_EP, ZZB_ZCL_PUMP, req, sizeof(req));
	check_frame(0, HVAC_EP, ZZB_ZCL_PUMP, rsp, sizeof(rsp));
	zassert_ok(zzb_zcl_attr_set(test_node(), HVAC_EP, ZZB_ZCL_PUMP,
				    ZZB_ZCL_PUMP_LIFETIME_ENERGY_CONSUMED, &energy));
	pump70.lifetime_energy_consumed = 0;
	zassert_ok(reboot());
	zassert_equal(pump70.operation_mode, 1);
	zassert_equal(pump70.control_mode, 7);
	zassert_equal(pump70.lifetime_running_hours, 0x123456);
	zassert_equal(pump70.lifetime_energy_consumed, 0x12345678);

	reset_fakes();
	reset_node();
	zassert_equal(pump70.operation_mode, 0);
	zassert_equal(pump70.control_mode, 0);
	zassert_equal(pump70.lifetime_running_hours, 0);
	zassert_equal(pump70.lifetime_energy_consumed, 0x12345678);
	check_zcl_cb(HVAC_EP, ZZB_ZCL_PUMP, reset_attrs, ARRAY_SIZE(reset_attrs));
}

ZTEST_SUITE(zzb_zcl_hvac, NULL, setup, before, after, NULL);
