/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/ballast.h>

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

static void set_hours(uint16_t attr, uint32_t hours)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), SENSOR_EP, ZZB_ZCL_BALLAST, attr, &hours));
}

static void set_lamp_alarm_mode(uint8_t mode)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), SENSOR_EP, ZZB_ZCL_BALLAST,
				    ZZB_ZCL_BALLAST_LAMP_ALARM_MODE, &mode));
}

/*
 * ZCL 5.3.2.2.2: MinLevel and MaxLevel stay within the physical levels and in order, and
 * BallastFactorAdjustment is 0x64 or above. The first command writes MinLevel 0x0f, MaxLevel 0xe1
 * and BallastFactorAdjustment 0x63, the second MinLevel 0x40 and BallastFactorAdjustment 0x64,
 * the third MaxLevel 0x3f, MinLevel 0xe1 and BallastFactorAdjustment 0xff.
 */
ZTEST(zzb_zcl_ballast, test_checks)
{
	static const uint8_t req1[] = {0x00, 0x40, 0x02, 0x10, 0x00, 0x20, 0x0f, 0x11,
				       0x00, 0x20, 0xe1, 0x15, 0x00, 0x20, 0x63};
	static const uint8_t rsp1[] = {0x18, 0x40, 0x04, 0x87, 0x10, 0x00,
				       0x87, 0x11, 0x00, 0x87, 0x15, 0x00};
	static const uint8_t req2[] = {0x00, 0x41, 0x02, 0x10, 0x00, 0x20,
				       0x40, 0x15, 0x00, 0x20, 0x64};
	static const uint8_t req3[] = {0x00, 0x42, 0x02, 0x11, 0x00, 0x20, 0x3f, 0x10,
				       0x00, 0x20, 0xe1, 0x15, 0x00, 0x20, 0xff};
	static const uint8_t rsp3[] = {0x18, 0x42, 0x04, 0x87, 0x11, 0x00, 0x87, 0x10, 0x00};

	receive_ep(SENSOR_EP, ZZB_ZCL_BALLAST, req1, sizeof(req1));
	check_frame(0, SENSOR_EP, ZZB_ZCL_BALLAST, rsp1, sizeof(rsp1));
	zassert_equal(ballast.min_level, PHYSICAL_MIN);
	zassert_equal(ballast.max_level, PHYSICAL_MAX);
	zassert_equal(ballast.ballast_factor_adjustment, 0xff);

	receive_ep(SENSOR_EP, ZZB_ZCL_BALLAST, req2, sizeof(req2));
	check_frame(1, SENSOR_EP, ZZB_ZCL_BALLAST, (uint8_t[]){0x18, 0x41, 0x04, 0x00}, 4);
	zassert_equal(ballast.min_level, 0x40);
	zassert_equal(ballast.ballast_factor_adjustment, 0x64);

	receive_ep(SENSOR_EP, ZZB_ZCL_BALLAST, req3, sizeof(req3));
	check_frame(2, SENSOR_EP, ZZB_ZCL_BALLAST, rsp3, sizeof(rsp3));
	zassert_equal(ballast.min_level, 0x40);
	zassert_equal(ballast.max_level, PHYSICAL_MAX);
	zassert_equal(ballast.ballast_factor_adjustment, 0xff);
}

/*
 * The settings are saved, and the reset restores the defaults, the physical levels for MinLevel
 * and MaxLevel. The records are MinLevel 0x20, IntrinsicBallastFactor 0x50, LampBurnHours 0x1234
 * and LampAlarmMode 0x01.
 */
ZTEST(zzb_zcl_ballast, test_settings)
{
	static const uint8_t req[] = {0x00, 0x43, 0x02, 0x10, 0x00, 0x20, 0x20,
				      0x14, 0x00, 0x20, 0x50, 0x33, 0x00, 0x22,
				      0x34, 0x12, 0x00, 0x34, 0x00, 0x18, 0x01};
	static const uint16_t reset_attrs[] = {0x0010, 0x0014, 0x0033, 0x0034};

	receive_ep(SENSOR_EP, ZZB_ZCL_BALLAST, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_BALLAST, (uint8_t[]){0x18, 0x43, 0x04, 0x00}, 4);

	zassert_ok(reboot());
	zassert_equal(ballast.min_level, 0x20);
	zassert_equal(ballast.intrinsic_ballast_factor, 0x50);
	zassert_equal(ballast.lamp_burn_hours, 0x1234);
	zassert_equal(ballast.lamp_alarm_mode, 0x01);
	zassert_equal(ballast.lamp_burn_hours_trip_point, 0xffffff);

	reset_fakes();
	reset_node();
	zassert_equal(ballast.min_level, PHYSICAL_MIN);
	zassert_equal(ballast.intrinsic_ballast_factor, 0xff);
	zassert_equal(ballast.lamp_burn_hours, 0);
	zassert_equal(ballast.lamp_alarm_mode, 0);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_BALLAST, reset_attrs, ARRAY_SIZE(reset_attrs));
}

/* Without records MinLevel and MaxLevel take the physical levels that the application set */
ZTEST(zzb_zcl_ballast, test_physical_defaults)
{
	ballast.physical_max_level = 0xd0;
	zassert_ok(reboot());
	zassert_equal(ballast.min_level, PHYSICAL_MIN);
	zassert_equal(ballast.max_level, 0xd0);

	ballast.physical_max_level = PHYSICAL_MAX;
	zassert_ok(reboot());
	zassert_equal(ballast.max_level, PHYSICAL_MAX);
}

/*
 * ZCL 5.3.2.2.4.6: LampBurnHours at or above LampBurnHoursTripPoint raises alarm 0x01 once, without
 * zcl_cb, and Alarm goes only with bit 0 of LampAlarmMode. A trip point of 0xffffff and unknown
 * burn hours give no alarm.
 */
ZTEST(zzb_zcl_ballast, test_lamp_burn_hours_alarm)
{
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT, 100);
	set_lamp_alarm_mode(0x01);

	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 99);
	zassert_equal(alarms30.alarm_count, 0);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 100);
	check_alarm(0, SENSOR_EP, ZZB_ZCL_BALLAST, 0x01);
	zassert_equal(zcl_cb_calls.count, 0);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 101);
	zassert_equal(alarms30.alarm_count, 1);

	/* A new lamp */
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 0);
	set_lamp_alarm_mode(0);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 100);
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_BALLAST, 0x01), 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);

	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 0xffffff);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT, 0xffffff);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 200);
	zassert_equal(alarms30.alarm_count, 2);
}

/*
 * A write of the trip point raises the alarm with zcl_cb for AlarmCount, and Reset Alarm raises it
 * again while LampBurnHours stays at or above the trip point (ZCL 3.11.2.4.1)
 */
ZTEST(zzb_zcl_ballast, test_lamp_burn_hours_reset_alarm)
{
	/* Write Attributes No Response: LampBurnHoursTripPoint 100 */
	static const uint8_t write[] = {0x10, 0x46, 0x05, 0x35, 0x00, 0x22, 0x64, 0x00, 0x00};
	static const uint16_t alarm_count[] = {ZZB_ZCL_ALARMS_ALARM_COUNT};

	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 150);
	receive_ep(SENSOR_EP, ZZB_ZCL_BALLAST, write, sizeof(write));
	zassert_equal(alarms30.alarm_count, 1);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ALARMS, alarm_count, 1);

	reset_alarm(SENSOR_EP, ZZB_ZCL_BALLAST, 0x00);
	zassert_equal(alarms30.alarm_count, 1);
	reset_alarm(SENSOR_EP, ZZB_ZCL_BALLAST, 0x01);
	zassert_equal(alarms30.alarm_count, 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	set_lamp_alarm_mode(0x01);
	reset_all_alarms(SENSOR_EP);
	zassert_equal(alarms30.alarm_count, 3);
	check_alarm(0, SENSOR_EP, ZZB_ZCL_BALLAST, 0x01);

	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 0);
	reset_all_alarms(SENSOR_EP);
	zassert_equal(alarms30.alarm_count, 3);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* The saved burn hours at or above the saved trip point raise the alarm again at boot */
ZTEST(zzb_zcl_ballast, test_lamp_burn_hours_at_boot)
{
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT, 100);
	set_hours(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 120);
	zassert_ok(reboot());
	zassert_equal(alarm_entries(SENSOR_EP, ZZB_ZCL_BALLAST, 0x01), 2);
}

ZTEST_SUITE(zzb_zcl_ballast, NULL, setup, before, after, NULL);
