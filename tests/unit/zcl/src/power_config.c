/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/power_config.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts with the measurements of fakes.c, the defaults and without records */
static void before(void *fixture)
{
	power_config.mains_voltage = 2300;
	power_config.battery_voltage = 30;
	power_config.battery_percentage_remaining = 200;
	power_config.battery_alarm_state = 0;
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static void set(uint16_t attr, const void *value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), SENSOR_EP, ZZB_ZCL_POWER_CONFIG, attr, value));
}

static void set_mains(uint16_t attr, uint16_t value)
{
	set(attr, &value);
}

static void set_mains_alarm_mask(uint8_t mask)
{
	set(ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK, &mask);
}

static void set_battery(uint16_t attr, uint8_t value)
{
	set(attr, &value);
}

static bool mains_timer_armed(void)
{
	return k_work_delayable_is_pending(&test_node()->zcl.mains_alarm_timer);
}

static const uint16_t alarm_count[] = {ZZB_ZCL_ALARMS_ALARM_COUNT};

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

/*
 * ZCL 3.3.2.2.2: MainsVoltage below MainsVoltageMinThreshold, or above MainsVoltageMaxThreshold,
 * for MainsVoltageDwellTripPoint seconds raises alarm 0x00, or 0x01, once. The timer comes with
 * zcl_cb for AlarmCount, and Alarm goes only with the bit of MainsAlarmMask.
 */
ZTEST(zzb_zcl_power_config, test_mains_voltage_alarms)
{
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 2000);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD, 2500);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT, 2);
	set_mains_alarm_mask(0x01);

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1900);
	zassert_true(mains_timer_armed());
	k_sleep(K_SECONDS(1));
	/* The dwell runs on while the condition lasts */
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1850);
	k_sleep(K_MSEC(900));
	/* Reset Alarm does not raise an alarm whose dwell has not passed */
	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x00);
	zassert_equal(alarms30.alarm_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(alarms30.alarm_count, 1);
	check_alarm(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x00);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);
	zassert_false(mains_timer_armed());

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1800);
	zassert_equal(alarms30.alarm_count, 1);

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 2600);
	k_sleep(K_MSEC(2100));
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x01), 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);

	/* A condition that ends within the dwell raises nothing and stops the timer */
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1900);
	k_sleep(K_SECONDS(1));
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 2300);
	zassert_false(mains_timer_armed());
	zassert_equal(alarms30.alarm_count, 2);

	/* The reset of the node ends the conditions */
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1900);
	reset_node();
	zassert_false(mains_timer_armed());
}

/*
 * A dwell of 0 raises the alarm at once: without zcl_cb when MainsVoltage causes it, with zcl_cb
 * when a write does. 0xffff is an unknown voltage or a threshold without alarm.
 */
ZTEST(zzb_zcl_power_config, test_mains_voltage_without_dwell)
{
	/* Write Attributes No Response: MainsVoltageMinThreshold 2400 */
	static const uint8_t write[] = {0x10, 0x43, 0x05, 0x11, 0x00, 0x21, 0x60, 0x09};

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 2000);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1900);
	zassert_equal(alarms30.alarm_count, 1);
	zassert_equal(zcl_cb_calls.count, 0);
	zassert_false(mains_timer_armed());

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 2300);
	receive_ep(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, write, sizeof(write));
	zassert_equal(alarms30.alarm_count, 2);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);

	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 0xffff);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD, 2000);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD, 0xffff);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 0xffff);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 2300);
	zassert_equal(alarms30.alarm_count, 2);
}

/* ZCL 3.11.2.4.1: Reset Alarm raises a mains voltage alarm again while its condition lasts */
ZTEST(zzb_zcl_power_config, test_mains_voltage_reset_alarm)
{
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 2000);
	set_mains_alarm_mask(0x01);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 1900);
	check_alarm(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x00);

	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x01);
	zassert_equal(alarms30.alarm_count, 1);
	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x00);
	zassert_equal(alarms30.alarm_count, 2);
	check_alarm(1, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x00);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);

	set_mains_alarm_mask(0);
	reset_all_alarms(SENSOR_EP);
	zassert_equal(alarms30.alarm_count, 3);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);

	set_mains_alarm_mask(0x01);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, 2300);
	reset_all_alarms(SENSOR_EP);
	zassert_equal(alarms30.alarm_count, 3);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* A condition at boot starts its dwell after the records are loaded */
ZTEST(zzb_zcl_power_config, test_mains_voltage_at_boot)
{
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 2000);
	set_mains(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT, 2);
	power_config.mains_voltage = 1900;
	zassert_ok(reboot());
	zassert_true(mains_timer_armed());
	k_sleep(K_MSEC(2100));
	zassert_equal(alarms30.alarm_count, 1);
}

/*
 * ZCL 3.3.2.2.4: BatteryVoltage below a threshold sets its bit of BatteryAlarmState and raises its
 * alarm at once, without zcl_cb, and Alarm goes only with the bit of BatteryAlarmMask. The bit is
 * cleared when the condition ends; the bits of the application stay.
 */
ZTEST(zzb_zcl_power_config, test_battery_voltage_alarms)
{
	static const uint16_t reset_attrs[] = {0x0035, 0x003e, 0x0036, 0x003e, 0x0037};
	uint32_t mains_lost = BIT(30);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, 25);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1, 28);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK, 0x02);
	set(ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE, &mains_lost);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 27);
	zassert_equal(power_config.battery_alarm_state, BIT(30) | BIT(1));
	check_alarm(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x11);
	zassert_equal(zcl_cb_calls.count, 0);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 24);
	zassert_equal(power_config.battery_alarm_state, BIT(30) | BIT(1) | BIT(0));
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x10), 1);
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x11), 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 30);
	zassert_equal(power_config.battery_alarm_state, BIT(30));

	/* The reset of the node ends the conditions and reports the changes of BatteryAlarmState */
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 24);
	reset_fakes();
	reset_node();
	zassert_equal(power_config.battery_alarm_state, BIT(30));
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, reset_attrs, ARRAY_SIZE(reset_attrs));
}

/*
 * The percentage thresholds are in percent and BatteryPercentageRemaining in half percent. A write
 * of a threshold changes BatteryAlarmState and AlarmCount with zcl_cb. 0xff is an unknown value or
 * a threshold without alarm.
 */
ZTEST(zzb_zcl_power_config, test_battery_percentage_alarms)
{
	/* Write Attributes No Response: BatteryPercentageThreshold3 101 */
	static const uint8_t write[] = {0x10, 0x44, 0x05, 0x3d, 0x00, 0x20, 0x65};
	static const uint16_t state[] = {ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE,
					 ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3};

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD2, 50);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING, 101);
	zassert_equal(power_config.battery_alarm_state, 0);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING, 99);
	zassert_equal(power_config.battery_alarm_state, BIT(2));
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x12), 1);
	zassert_equal(zcl_cb_calls.count, 0);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING, 200);
	receive_ep(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, write, sizeof(write));
	zassert_equal(power_config.battery_alarm_state, BIT(3));
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x13), 1);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, state, ARRAY_SIZE(state));
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3, 0xff);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING, 0xff);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_MIN_THRESHOLD, 0xfe);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, 0xff);
	zassert_equal(power_config.battery_alarm_state, 0);
	zassert_equal(alarms30.alarm_count, 2);
}

/* ZCL 3.11.2.4.1: Reset Alarm raises a battery alarm again while its condition lasts */
ZTEST(zzb_zcl_power_config, test_battery_reset_alarm)
{
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, 25);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1, 28);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK, 0x01);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 24);
	check_alarm(0, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x10);
	zassert_equal(alarms30.alarm_count, 2);

	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x12);
	zassert_equal(alarms30.alarm_count, 2);
	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x11);
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x11), 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);
	reset_alarm(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x10);
	check_alarm(1, SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x10);

	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 30);
	reset_all_alarms(SENSOR_EP);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* BatteryAlarmState is not saved: a condition at boot sets its bit and raises its alarm again */
ZTEST(zzb_zcl_power_config, test_battery_at_boot)
{
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, 25);
	set_battery(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, 24);
	zassert_ok(reboot());
	zassert_equal(power_config.battery_alarm_state, BIT(0));
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_POWER_CONFIG, 0x10), 2);
}

ZTEST_SUITE(zzb_zcl_power_config, NULL, setup, before, after, NULL);
