/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/binary_input.h>
#include <zzb/zcl/on_off_switch_config.h>

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
	input121 = (struct zzb_zcl_binary_input){0};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/*
 * ZCL Table 3-53: SwitchActions is 0 to 2, and SwitchType is read-only. The first command writes
 * SwitchActions 3 and SwitchType 0x00, the second SwitchActions 2.
 */
ZTEST(zzb_zcl_binary_input, test_switch_actions_checks)
{
	static const uint8_t req1[] = {0x00, 0x40, 0x02, 0x10, 0x00, 0x30,
				       0x03, 0x00, 0x00, 0x30, 0x00};
	static const uint8_t rsp1[] = {0x18, 0x40, 0x04, 0x87, 0x10, 0x00, 0x88, 0x00, 0x00};
	static const uint8_t req2[] = {0x00, 0x41, 0x02, 0x10, 0x00, 0x30, 0x02};
	static const uint8_t rsp2[] = {0x18, 0x41, 0x04, 0x00};

	receive_ep(SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, req1, sizeof(req1));
	check_frame(0, SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, rsp1, sizeof(rsp1));
	zassert_equal(switch120.switch_actions, 0);
	zassert_equal(switch120.switch_type, 0x01);

	receive_ep(SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, req2, sizeof(req2));
	check_frame(1, SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, rsp2, sizeof(rsp2));
	zassert_equal(switch120.switch_actions, 2);
}

/* SwitchActions is 0 without records, is saved, and the reset sets it to 0 */
ZTEST(zzb_zcl_binary_input, test_switch_actions_settings)
{
	static const uint8_t req[] = {0x00, 0x42, 0x02, 0x10, 0x00, 0x30, 0x01};
	static const uint8_t rsp[] = {0x18, 0x42, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS};

	switch120.switch_actions = 2;
	zassert_ok(reboot());
	zassert_equal(switch120.switch_actions, 0);

	receive_ep(SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, req, sizeof(req));
	check_frame(0, SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, rsp, sizeof(rsp));
	zassert_ok(reboot());
	zassert_equal(switch120.switch_actions, 1);

	reset_fakes();
	reset_node();
	zassert_equal(switch120.switch_actions, 0);
	check_zcl_cb(SWITCH_EP, ZZB_ZCL_ON_OFF_SWITCH_CONFIG, reset_attrs, ARRAY_SIZE(reset_attrs));
}

/*
 * ZCL 3.14.11.2: PresentValue is writable only while OutOfService is TRUE, otherwise READ_ONLY, and
 * a value other than 0 and 1 gives INVALID_VALUE first. The first command writes PresentValue 1
 * and 2, the second OutOfService TRUE and PresentValue 1, which is checked against the values
 * before the command, the third PresentValue 1.
 */
ZTEST(zzb_zcl_binary_input, test_present_value_writes)
{
	static const uint8_t req1[] = {0x00, 0x43, 0x02, 0x55, 0x00, 0x10,
				       0x01, 0x55, 0x00, 0x10, 0x02};
	static const uint8_t rsp1[] = {0x18, 0x43, 0x04, 0x88, 0x55, 0x00, 0x87, 0x55, 0x00};
	static const uint8_t req2[] = {0x00, 0x44, 0x02, 0x51, 0x00, 0x10,
				       0x01, 0x55, 0x00, 0x10, 0x01};
	static const uint8_t rsp2[] = {0x18, 0x44, 0x04, 0x88, 0x55, 0x00};
	static const uint8_t req3[] = {0x00, 0x45, 0x02, 0x55, 0x00, 0x10, 0x01};
	static const uint8_t rsp3[] = {0x18, 0x45, 0x04, 0x00};

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, req1, sizeof(req1));
	check_frame(0, INPUT_EP, ZZB_ZCL_BINARY_INPUT, rsp1, sizeof(rsp1));
	zassert_false(input121.present_value);

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, req2, sizeof(req2));
	check_frame(1, INPUT_EP, ZZB_ZCL_BINARY_INPUT, rsp2, sizeof(rsp2));
	zassert_true(input121.out_of_service);
	zassert_false(input121.present_value);

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, req3, sizeof(req3));
	check_frame(2, INPUT_EP, ZZB_ZCL_BINARY_INPUT, rsp3, sizeof(rsp3));
	zassert_true(input121.present_value);
}

static void input_set(uint16_t attr, uint8_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), INPUT_EP, ZZB_ZCL_BINARY_INPUT, attr, &value));
}

/*
 * ZCL 3.14.11.3: the stack keeps bit 3 of StatusFlags equal to OutOfService and bit 1 set while
 * Reliability is not 0, also when the application sets StatusFlags. Only the changes that follow
 * OutOfService come with zcl_cb.
 */
ZTEST(zzb_zcl_binary_input, test_status_flags)
{
	static const uint16_t attrs[] = {ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS,
					 ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS};

	input_set(ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE, true);
	zassert_equal(input121.status_flags, 0x08);
	input_set(ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS, 0x01);
	zassert_equal(input121.status_flags, 0x09);
	input_set(ZZB_ZCL_BINARY_INPUT_RELIABILITY, 7);
	zassert_equal(input121.status_flags, 0x0b);
	input_set(ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS, 0x00);
	zassert_equal(input121.status_flags, 0x0a);
	input_set(ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE, false);
	zassert_equal(input121.status_flags, 0x02);
	input_set(ZZB_ZCL_BINARY_INPUT_RELIABILITY, 0);
	zassert_equal(input121.status_flags, 0x00);
	check_zcl_cb(INPUT_EP, ZZB_ZCL_BINARY_INPUT, attrs, ARRAY_SIZE(attrs));
}

/*
 * ZCL 3.14.5.4.4: PresentValue and StatusFlags are reportable. After the Configure Reporting of
 * ZHA 2.3.1 for PresentValue (0 s, 900 s) and one for StatusFlags (0 s, 900 s), Reliability 3 set
 * by the application and OutOfService TRUE written remotely are reported as StatusFlags 0x02 and
 * 0x0a.
 */
ZTEST(zzb_zcl_binary_input, test_status_flags_report)
{
	static const uint8_t configure[] = {0x00, 0x46, 0x06, 0x00, 0x55, 0x00, 0x10,
					    0x00, 0x00, 0x84, 0x03, 0x00, 0x6f, 0x00,
					    0x18, 0x00, 0x00, 0x84, 0x03};
	static const uint8_t configure_rsp[] = {0x18, 0x46, 0x07, 0x00};
	static const uint8_t write[] = {0x00, 0x47, 0x02, 0x51, 0x00, 0x10, 0x01};
	static const uint8_t write_rsp[] = {0x18, 0x47, 0x04, 0x00};

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, configure, sizeof(configure));
	check_frame(0, INPUT_EP, ZZB_ZCL_BINARY_INPUT, configure_rsp, sizeof(configure_rsp));

	input_set(ZZB_ZCL_BINARY_INPUT_RELIABILITY, 3);
	k_sleep(K_MSEC(10));
	check_report_frame(1, INPUT_EP, ZZB_ZCL_BINARY_INPUT, (uint8_t[]){0x6f, 0x00, 0x18, 0x02},
			   4);

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, write, sizeof(write));
	check_frame(2, INPUT_EP, ZZB_ZCL_BINARY_INPUT, write_rsp, sizeof(write_rsp));
	k_sleep(K_MSEC(10));
	check_report_frame(3, INPUT_EP, ZZB_ZCL_BINARY_INPUT, (uint8_t[]){0x6f, 0x00, 0x18, 0x0a},
			   4);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
}

/*
 * OutOfService is FALSE without records and is saved; after a reboot it sets bit 3 of StatusFlags
 * again. The reset sets it FALSE and clears the bit.
 */
ZTEST(zzb_zcl_binary_input, test_out_of_service_settings)
{
	static const uint8_t req[] = {0x00, 0x48, 0x02, 0x51, 0x00, 0x10, 0x01};
	static const uint8_t rsp[] = {0x18, 0x48, 0x04, 0x00};
	static const uint16_t reset_attrs[] = {ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS,
					       ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE};

	input121.out_of_service = true;
	zassert_ok(reboot());
	zassert_false(input121.out_of_service);

	receive_ep(INPUT_EP, ZZB_ZCL_BINARY_INPUT, req, sizeof(req));
	check_frame(0, INPUT_EP, ZZB_ZCL_BINARY_INPUT, rsp, sizeof(rsp));
	input121 = (struct zzb_zcl_binary_input){0};
	zassert_ok(reboot());
	zassert_true(input121.out_of_service);
	zassert_equal(input121.status_flags, 0x08);

	reset_fakes();
	reset_node();
	zassert_false(input121.out_of_service);
	zassert_equal(input121.status_flags, 0x00);
	check_zcl_cb(INPUT_EP, ZZB_ZCL_BINARY_INPUT, reset_attrs, ARRAY_SIZE(reset_attrs));
}

ZTEST_SUITE(zzb_zcl_binary_input, NULL, setup, before, after, NULL);
