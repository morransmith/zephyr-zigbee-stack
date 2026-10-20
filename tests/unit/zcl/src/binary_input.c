/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
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

ZTEST_SUITE(zzb_zcl_binary_input, NULL, setup, before, after, NULL);
