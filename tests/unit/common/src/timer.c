/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "common/timer.h"

static struct k_work_delayable dwork;
static int runs;
static bool rearm;

static void handler(struct k_work *work)
{
	runs++;
	if (rearm) {
		rearm = false;
		zzb_timer_arm(&dwork, K_MSEC(100));
	}
}

static void before(void *fixture)
{
	k_work_init_delayable(&dwork, handler);
	runs = 0;
	rearm = false;
}

ZTEST(zzb_timer, test_arm_keeps_earliest_deadline)
{
	zzb_timer_arm(&dwork, K_MSEC(100));
	zzb_timer_arm(&dwork, K_MSEC(50));
	zzb_timer_arm(&dwork, K_MSEC(200));

	k_sleep(K_MSEC(40));
	zassert_equal(runs, 0);
	k_sleep(K_MSEC(20));
	zassert_equal(runs, 1);
	k_sleep(K_MSEC(190));
	zassert_equal(runs, 1);
}

ZTEST(zzb_timer, test_handler_rearms_itself)
{
	rearm = true;
	zzb_timer_arm(&dwork, K_MSEC(50));

	k_sleep(K_MSEC(60));
	zassert_equal(runs, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(runs, 2);
}

ZTEST_SUITE(zzb_timer, NULL, NULL, before, NULL, NULL);
