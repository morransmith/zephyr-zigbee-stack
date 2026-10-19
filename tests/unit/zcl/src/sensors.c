/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/illuminance.h>

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
 * ZCL 4.3.2.4.1: IlluminanceTargetLevel is written and saved, and the reset restores 0xffff, the
 * value that is not valid, which a write may also set
 */
ZTEST(zzb_zcl_sensors, test_illuminance_target_level)
{
	static const uint8_t req[] = {0x00, 0x40, 0x02, 0x10, 0x00, 0x21, 0x34, 0x12};
	static const uint8_t rsp[] = {0x18, 0x40, 0x04, 0x00};
	static const uint8_t not_valid[] = {0x00, 0x41, 0x02, 0x10, 0x00, 0x21, 0xff, 0xff};
	static const uint8_t not_valid_rsp[] = {0x18, 0x41, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {0x0010};

	zassert_equal(illuminance_level.illuminance_target_level, 0xffff);
	receive_ep(SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, req, sizeof(req));
	check_frame(0, SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, rsp, sizeof(rsp));
	zassert_ok(reboot());
	zassert_equal(illuminance_level.illuminance_target_level, 0x1234);

	reset_fakes();
	reset_node();
	zassert_equal(illuminance_level.illuminance_target_level, 0xffff);
	check_zcl_cb(SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, reset_attrs, ARRAY_SIZE(reset_attrs));

	receive_ep(SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, req, sizeof(req));
	receive_ep(SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, not_valid, sizeof(not_valid));
	check_frame(1, SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, not_valid_rsp, sizeof(not_valid_rsp));
	zassert_equal(illuminance_level.illuminance_target_level, 0xffff);
}

/*
 * ZCL 4.2.2.5, 4.3.2.7: MeasuredValue (minimum interval 30 s, maximum 900 s, change 1) and
 * LevelStatus (0 s, 900 s) are reportable
 */
ZTEST(zzb_zcl_sensors, test_reportable)
{
	static const uint8_t measured[] = {0x00, 0x42, 0x06, 0x00, 0x00, 0x00, 0x21,
					   0x1e, 0x00, 0x84, 0x03, 0x01, 0x00};
	static const uint8_t measured_rsp[] = {0x18, 0x42, 0x07, 0x00};
	static const uint8_t status[] = {0x00, 0x43, 0x06, 0x00, 0x00, 0x00,
					 0x30, 0x00, 0x00, 0x84, 0x03};
	static const uint8_t status_rsp[] = {0x18, 0x43, 0x07, 0x00};

	receive_ep(SENSOR_EP, ZZB_ZCL_ILLUMINANCE, measured, sizeof(measured));
	check_frame(0, SENSOR_EP, ZZB_ZCL_ILLUMINANCE, measured_rsp, sizeof(measured_rsp));
	receive_ep(SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, status, sizeof(status));
	check_frame(1, SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL, status_rsp, sizeof(status_rsp));
}

ZTEST_SUITE(zzb_zcl_sensors, NULL, setup, before, after, NULL);
