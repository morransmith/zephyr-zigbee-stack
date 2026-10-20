/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/metering.h>

#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts without reports */
static void before(void *fixture)
{
	reset_node();
	reset_fakes();
	meter122 = (struct zzb_zcl_metering){0};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/*
 * One Configure Reporting with the reports that ZHA 2.3.1 configures: InstantaneousDemand (int24)
 * 5/900/1, CurrentSummationDelivered (uint48) 30/900/1 and Status (map8) 1/900. A negative demand
 * and a summation above 32 bits are reported after their minimum intervals.
 */
ZTEST(zzb_zcl_metering, test_zha_reports)
{
	static const uint8_t configure[] = {0x00, 0x50, 0x06, 0x00, 0x00, 0x04, 0x2a, 0x05, 0x00,
					    0x84, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25,
					    0x1e, 0x00, 0x84, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x02, 0x18, 0x01, 0x00, 0x84, 0x03};
	static const uint8_t configure_rsp[] = {0x18, 0x50, 0x07, 0x00};
	static const uint8_t demand_report[] = {0x00, 0x04, 0x2a, 0x18, 0xfc, 0xff};
	static const uint8_t summation_report[] = {0x00, 0x00, 0x25, 0xab, 0x89,
						   0x67, 0x45, 0x23, 0x01};
	int32_t demand = -1000;
	uint64_t summation = 0x0123456789ab;

	meter122.current_summation_delivered = 0xffffffff;
	meter122.instantaneous_demand = 100;
	receive_ep(METER_EP, ZZB_ZCL_METERING, configure, sizeof(configure));
	check_frame(0, METER_EP, ZZB_ZCL_METERING, configure_rsp, sizeof(configure_rsp));

	zassert_ok(zzb_zcl_attr_set(test_node(), METER_EP, ZZB_ZCL_METERING,
				    ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND, &demand));
	zassert_ok(zzb_zcl_attr_set(test_node(), METER_EP, ZZB_ZCL_METERING,
				    ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED, &summation));
	k_sleep(K_MSEC(5100));
	check_report_frame(1, METER_EP, ZZB_ZCL_METERING, demand_report, sizeof(demand_report));

	k_sleep(K_MSEC(25000));
	check_report_frame(2, METER_EP, ZZB_ZCL_METERING, summation_report,
			   sizeof(summation_report));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
}

ZTEST_SUITE(zzb_zcl_metering, NULL, setup, before, after, NULL);
