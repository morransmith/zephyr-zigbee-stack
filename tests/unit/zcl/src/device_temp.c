/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/device_temp.h>

#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts without a temperature, with the defaults and without records */
static void before(void *fixture)
{
	device_temp.current_temperature = INT16_MIN;
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static void set(uint16_t attr, const void *value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, attr, value));
}

static void set_temperature(int16_t t)
{
	set(ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE, &t);
}

static void check_experienced(int16_t min, int16_t max)
{
	zassert_equal(device_temp.min_temp_experienced, min);
	zassert_equal(device_temp.max_temp_experienced, max);
}

/* ZCL 3.4.2.2.1: the stack keeps the extremes of the known temperatures, without zcl_cb */
ZTEST(zzb_zcl_device_temp, test_experienced)
{
	check_experienced(INT16_MIN, INT16_MIN);
	set_temperature(25);
	check_experienced(25, 25);
	set_temperature(20);
	set_temperature(30);
	check_experienced(20, 30);
	set_temperature(INT16_MIN);
	check_experienced(20, 30);
	set_temperature(-5);
	check_experienced(-5, 30);

	/* Saved; a temperature that the application sets before the start counts too */
	device_temp.current_temperature = 35;
	zassert_ok(reboot());
	check_experienced(-5, 35);
	zassert_equal(zcl_cb_calls.count, 0);
}

/* OverTempTotalDwell of the application is saved, and its record replaces the value at boot */
ZTEST(zzb_zcl_device_temp, test_over_temp_total_dwell)
{
	uint16_t hours = 7;

	set(ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL, &hours);
	device_temp.over_temp_total_dwell = 0;
	zassert_ok(reboot());
	zassert_equal(device_temp.over_temp_total_dwell, 7);
}

/*
 * ZCL 3.4.2.2.2: the alarm settings are written and saved, and the reset restores the defaults of
 * the settings and of the extremes. The records are DeviceTempAlarmMask, LowTempThreshold -10 and
 * HighTempDwellTripPoint 3600.
 */
ZTEST(zzb_zcl_device_temp, test_settings)
{
	static const uint8_t req[] = {0x00, 0x40, 0x02, 0x10, 0x00, 0x18, 0x01, 0x11, 0x00,
				      0x29, 0xf6, 0xff, 0x14, 0x00, 0x22, 0x10, 0x0e, 0x00};
	static const uint8_t rsp[] = {0x18, 0x40, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {0x0001, 0x0002, 0x0010, 0x0011, 0x0014};

	zassert_equal(device_temp.high_temp_dwell_trip_point, 0xffffff);
	receive_ep(SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, rsp, sizeof(rsp));
	set_temperature(25);

	zassert_ok(reboot());
	zassert_equal(device_temp.alarm_mask, 0x01);
	zassert_equal(device_temp.low_temp_threshold, -10);
	zassert_equal(device_temp.high_temp_threshold, INT16_MIN);
	zassert_equal(device_temp.high_temp_dwell_trip_point, 3600);

	reset_fakes();
	reset_node();
	zassert_equal(device_temp.alarm_mask, 0);
	zassert_equal(device_temp.low_temp_threshold, INT16_MIN);
	zassert_equal(device_temp.high_temp_dwell_trip_point, 0xffffff);
	check_experienced(INT16_MIN, INT16_MIN);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, reset_attrs, ARRAY_SIZE(reset_attrs));
}

/* CurrentTemperature is reportable for ZHA, although ZCL does not mark it */
ZTEST(zzb_zcl_device_temp, test_reportable)
{
	static const uint8_t req[] = {0x00, 0x41, 0x06, 0x00, 0x00, 0x00, 0x29,
				      0x1e, 0x00, 0x84, 0x03, 0x32, 0x00};
	static const uint8_t rsp[] = {0x18, 0x41, 0x07, 0x00};

	receive_ep(SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_DEVICE_TEMP, rsp, sizeof(rsp));
}

ZTEST_SUITE(zzb_zcl_device_temp, NULL, setup, before, after, NULL);
