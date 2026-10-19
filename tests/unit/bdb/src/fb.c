/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "fakes.h"

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

/* BDB 8.4 step 1 */
ZTEST(zzb_bdb_fb, test_target_off_a_network)
{
	reboot(false);
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_NOT_ON_A_NETWORK);
	zassert_equal(zzb_zcl_identify_fake.call_count, 0);
}

/* BDB 8.4 steps 3 to 5: the endpoint identifies for bdbcMinCommissioningTime */
ZTEST(zzb_bdb_fb, test_target)
{
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	zassert_equal(zzb_zcl_identify_fake.call_count, 1);
	zassert_equal(zzb_zcl_identify_fake.arg1_val, IDENTIFY_EP);
	zassert_equal(zzb_zcl_identify_fake.arg2_val, 180);
	zassert_equal(zzb_bdb_finding_binding_target(node, IDENTIFY_EP), -EBUSY);

	/* Steering and the other procedures stay available */
	zassert_ok(zzb_bdb_steering(node));
	events.count = 0;

	zzb_bdb_identify_end(node, OTHER_EP);
	zassert_equal(events.count, 0);
	zzb_bdb_identify_end(node, IDENTIFY_EP);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_SUCCESS);

	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
}

ZTEST(zzb_bdb_fb, test_target_without_an_identify_server)
{
	zassert_equal(zzb_bdb_finding_binding_target(node, OTHER_EP), -ENOENT);
	zassert_equal(zzb_bdb_finding_binding_target(node, 9), -ENOENT);
	zassert_equal(zzb_zcl_identify_fake.call_count, 0);
}

static void identify_ends(struct zzb_node *n)
{
	zzb_bdb_identify_end(n, IDENTIFY_EP);
}

/* The reset of the node, whose reset of IdentifyTime reaches BDB, ends the target without event */
ZTEST(zzb_bdb_fb, test_target_and_reset)
{
	zzb_zcl_reset_fake.custom_fake = identify_ends;
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	zzb_nlme_leave_ind(node, 0, false);
	check_event(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

ZTEST_SUITE(zzb_bdb_fb, NULL, setup, before, NULL, NULL);
