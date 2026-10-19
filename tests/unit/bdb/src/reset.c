/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "fakes.h"
#include "mac/mac.h"
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

/* NLME-RESET, the APS and ZDO state, and bdbNodeIsOnANetwork FALSE */
static void check_reset(enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 1);
	zassert_equal(zzb_aps_reset_fake.call_count, 1);
	zassert_equal(zzb_zdo_init_fake.call_count, 1);
	zassert_equal(events.count, 1);
	zassert_equal(events.event, event);
	zassert_equal(events.status, status);

	zassert_ok(zzb_bdb_start(node));
	zassert_equal(events.status, ZZB_BDB_NOT_ON_A_NETWORK);
}

/* BDB 9.5 */
ZTEST(zzb_bdb_reset, test_reset_leaves_the_network)
{
	zassert_ok(zzb_bdb_reset(node));
	zassert_equal(zzb_nlme_leave_req_fake.call_count, 1);
	zassert_false(zzb_nlme_leave_req_fake.arg1_val);
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 0);
	zassert_equal(zzb_bdb_steering(node), -EBUSY);

	zzb_nlme_leave_conf(node, ZZB_MAC_STATUS_SUCCESS);
	check_reset(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

/* A node that has lost its parent cannot send the leave command */
ZTEST(zzb_bdb_reset, test_reset_without_a_parent)
{
	zzb_nlme_leave_req_fake.return_val = -ENOTCONN;
	zassert_ok(zzb_bdb_reset(node));
	check_reset(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

ZTEST(zzb_bdb_reset, test_reset_off_a_network)
{
	reboot(false);
	zassert_ok(zzb_bdb_reset(node));
	zassert_equal(zzb_nlme_leave_req_fake.call_count, 0);
	check_reset(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

/* NWK runs a procedure, such as a leave requested by the parent */
ZTEST(zzb_bdb_reset, test_reset_while_nwk_is_busy)
{
	zzb_nlme_leave_req_fake.return_val = -EBUSY;
	zassert_equal(zzb_bdb_reset(node), -EBUSY);

	reboot(false);
	zzb_nlme_reset_req_fake.return_val = -EBUSY;
	zassert_equal(zzb_bdb_reset(node), -EBUSY);
	zassert_equal(zzb_aps_reset_fake.call_count, 0);
	zassert_equal(events.count, 0);
}

ZTEST(zzb_bdb_reset, test_reset_cancels_the_pause_between_rejoins)
{
	zzb_nlme_join_req_fake.return_val = -ENETUNREACH;
	zassert_ok(zzb_bdb_start(node));
	run_stack();
	zassert_equal(events.status, ZZB_BDB_NO_NETWORK);

	zzb_nlme_leave_req_fake.return_val = -ENOTCONN;
	zassert_ok(zzb_bdb_reset(node));
	zassert_equal(events.count, 2);
	zassert_equal(events.event, ZZB_BDB_EVENT_LEFT);
	k_sleep(K_SECONDS(31));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 3);
}

/* BDB 9.3 */
ZTEST(zzb_bdb_reset, test_leave_request_resets_the_node)
{
	zzb_nlme_leave_ind(node, 0, false);
	check_reset(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

/* R22 3.6.1.10.4: the node keeps its network and rejoins */
ZTEST(zzb_bdb_reset, test_leave_request_with_rejoin)
{
	zzb_nlme_leave_ind(node, 0, true);
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);
	zassert_equal(zzb_nlme_join_req_fake.arg2_val, ZZB_NWK_JOIN_REJOIN);

	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(events.count, 1);
	zassert_equal(events.event, ZZB_BDB_EVENT_REJOIN);
	zassert_equal(events.status, ZZB_BDB_SUCCESS);
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 0);
}

/* NWK reports a lost parent with parent link failure */
ZTEST(zzb_bdb_reset, test_leave_of_a_neighbor_is_ignored)
{
	zzb_nlme_leave_ind(node, 0x00124b0000000001ULL, false);
	run_stack();
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 0);
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);
	zassert_equal(events.count, 0);
}

ZTEST_SUITE(zzb_bdb_reset, NULL, setup, before, NULL, NULL);
