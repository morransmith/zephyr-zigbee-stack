/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/time.h>

#include "fakes.h"
#include "zcl/zcl.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* Section 16: the clock of the node runs from its setting and the reset of the node clears it */
ZTEST(zzb_zcl_time, test_clock)
{
	struct zzb_node *node = test_node();
	uint32_t utc;
	uint32_t local;

	zassert_equal(zzb_zcl_time_get(node, &utc, &local), -ENODATA);
	zassert_equal(zzb_zcl_clock_utc(node), 0xffffffff);
	zassert_equal(zzb_zcl_clock_local(node), 0xffffffff);

	zzb_zcl_clock_set(node, 1000, -3600);
	k_sleep(K_MSEC(5500));
	zassert_ok(zzb_zcl_time_get(node, &utc, &local));
	zassert_equal(utc, 1005);
	zassert_equal(local, 1005 - 3600);
	zassert_equal(zzb_zcl_clock_utc(node), 1005);
	zassert_equal(zzb_zcl_clock_local(node), 1005 - 3600);

	reset_node();
	zassert_equal(zzb_zcl_time_get(node, &utc, &local), -ENODATA);
}

ZTEST_SUITE(zzb_zcl_time, NULL, setup, before, after, NULL);
