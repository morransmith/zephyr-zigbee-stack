/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/power_config.h>

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
 * ZCL 3.3.2.2.2, 3.3.2.2.4: the alarm settings are written and saved, and the reset restores their
 * defaults. The records are MainsAlarmMask, MainsVoltageMaxThreshold, BatteryVoltageThreshold3 and
 * BatteryPercentageMinThreshold.
 */
ZTEST(zzb_zcl_power_config, test_settings)
{
	static const uint8_t req[] = {0x00, 0x40, 0x02, 0x10, 0x00, 0x18, 0x03, 0x12, 0x00, 0x21,
				      0x00, 0x09, 0x39, 0x00, 0x20, 0x1c, 0x3a, 0x00, 0x20, 0x05};
	static const uint8_t rsp[] = {0x18, 0x40, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {0x0010, 0x0012, 0x0039, 0x003a};

	zassert_equal(power_config.mains_voltage_max_threshold, 0xffff);
	receive_ep(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, rsp, sizeof(rsp));

	zassert_ok(reboot());
	zassert_equal(power_config.mains_alarm_mask, 0x03);
	zassert_equal(power_config.mains_voltage_max_threshold, 0x0900);
	zassert_equal(power_config.battery_voltage_threshold3, 0x1c);
	zassert_equal(power_config.battery_percentage_min_threshold, 0x05);
	/* The values of the application stay */
	zassert_equal(power_config.battery_size, 0x03);

	reset_fakes();
	reset_node();
	zassert_equal(power_config.mains_alarm_mask, 0);
	zassert_equal(power_config.mains_voltage_max_threshold, 0xffff);
	zassert_equal(power_config.battery_voltage_threshold3, 0);
	zassert_equal(power_config.battery_percentage_min_threshold, 0);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, reset_attrs, ARRAY_SIZE(reset_attrs));

	zassert_ok(reboot());
	zassert_equal(power_config.mains_voltage_max_threshold, 0xffff);
}

/* Section 16: the battery is described by the application, although ZCL makes it writable */
ZTEST(zzb_zcl_power_config, test_read_only)
{
	static const uint8_t req[] = {0x00, 0x41, 0x02, 0x31, 0x00, 0x30, 0x04,
				      0x3e, 0x00, 0x1b, 0x01, 0x00, 0x00, 0x00};
	static const uint8_t rsp[] = {0x18, 0x41, 0x04, 0x88, 0x31, 0x00, 0x88, 0x3e, 0x00};

	receive_ep(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, rsp, sizeof(rsp));
}

/*
 * BatteryVoltage, BatteryPercentageRemaining and BatteryAlarmState are reportable, the first for
 * ZHA although ZCL does not mark it; MainsVoltage is not
 */
ZTEST(zzb_zcl_power_config, test_reportable)
{
	static const uint8_t req[] = {0x00, 0x42, 0x06, 0x00, 0x20, 0x00, 0x20, 0x10, 0x0e, 0x30,
				      0x2a, 0x01, 0x00, 0x21, 0x00, 0x20, 0x10, 0x0e, 0x30, 0x2a,
				      0x01, 0x00, 0x3e, 0x00, 0x1b, 0x00, 0x00, 0x84, 0x03, 0x00,
				      0x00, 0x00, 0x21, 0x00, 0x00, 0x84, 0x03, 0x01, 0x00};
	static const uint8_t rsp[] = {0x18, 0x42, 0x07, 0x8c, 0x00, 0x00, 0x00};

	receive_ep(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, rsp, sizeof(rsp));
}

ZTEST_SUITE(zzb_zcl_power_config, NULL, setup, before, after, NULL);
