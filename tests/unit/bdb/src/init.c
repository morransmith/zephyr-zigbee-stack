/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "fakes.h"
#include "nwk/nwk.h"

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	reboot(true);
}

static void check_event(enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	zassert_equal(events.count, 1);
	zassert_equal(events.event, event);
	zassert_equal(events.status, status);
}

/* Checks NLME-JOIN.request i of the rejoin series */
static void check_rejoin(int i, uint32_t channels, uint8_t duration, bool secure)
{
	zassert_true(zzb_nlme_join_req_fake.call_count > i);
	zassert_equal(zzb_nlme_join_req_fake.arg1_history[i], EXT_PAN_ID);
	zassert_equal(zzb_nlme_join_req_fake.arg2_history[i], ZZB_NWK_JOIN_REJOIN);
	zassert_equal(zzb_nlme_join_req_fake.arg3_history[i], channels);
	zassert_equal(zzb_nlme_join_req_fake.arg4_history[i], duration);
	zassert_equal(zzb_nlme_join_req_fake.arg5_history[i], CAPABILITY);
	zassert_equal(zzb_nlme_join_req_fake.arg6_history[i], secure);
}

/* BDB 7.1 step 2 */
ZTEST(zzb_bdb_init, test_start_off_a_network)
{
	reboot(false);
	zassert_ok(zzb_bdb_start(node));
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NOT_ON_A_NETWORK);

	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);
}

/* BDB 7.1 steps 4 and 5 */
ZTEST(zzb_bdb_init, test_start_rejoins)
{
	zassert_ok(zzb_bdb_start(node));
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);
	check_rejoin(0, 0, 0, true);
	zassert_equal(zzb_bdb_start(node), -EBUSY);
	zassert_equal(events.count, 0);

	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 1);
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_SUCCESS);
}

/* The MAC refuses a scan while its TX FIFO holds frames */
ZTEST(zzb_bdb_init, test_busy_rejoin_is_repeated)
{
	zzb_nlme_join_req_fake.return_val = -EBUSY;
	zassert_ok(zzb_bdb_start(node));
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);

	k_sleep(K_MSEC(990));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);
	zzb_nlme_join_req_fake.return_val = 0;
	k_sleep(K_MSEC(20));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 2);
	check_rejoin(1, 0, 0, true);
	zassert_equal(events.count, 0);
}

ZTEST(zzb_bdb_init, test_failed_rejoin)
{
	zassert_ok(zzb_bdb_start(node));
	run_stack();
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_NO_NETWORKS);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 0);
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NO_NETWORK);
}

ZTEST_SUITE(zzb_bdb_init, NULL, setup, before, NULL, NULL);
