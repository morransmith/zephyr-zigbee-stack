/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "fakes.h"
#include "nwk/nwk.h"

#define ALL_CHANNELS 0x07fff800

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

	k_sleep(K_MSEC(900));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);
	zzb_nlme_join_req_fake.return_val = 0;
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 2);
	check_rejoin(1, 0, 0, true);
	zassert_equal(events.count, 0);
}

/* Secure on the channel of the network, secure on all channels, then a trust center rejoin */
static void fail_two_steps(void)
{
	run_stack();
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_NO_NETWORKS);
	run_stack();
	check_rejoin(1, ALL_CHANNELS, 3, true);
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_NOT_PERMITTED);
	run_stack();
	check_rejoin(2, ALL_CHANNELS, 3, false);
}

ZTEST(zzb_bdb_init, test_trust_center_rejoin_after_failed_secure_rejoins)
{
	zassert_ok(zzb_bdb_start(node));
	fail_two_steps();

	/* The network key arrives from the trust center */
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 0);
	zzb_bdb_nwk_key_ind(node);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 1);
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_SUCCESS);
}

/* apsSecurityTimeOutPeriod, then the pause before the next series */
ZTEST(zzb_bdb_init, test_network_key_does_not_arrive)
{
	zassert_ok(zzb_bdb_start(node));
	fail_two_steps();
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);

	k_sleep(K_MSEC(1600));
	zassert_equal(events.count, 0);
	k_sleep(K_MSEC(200));
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NO_NETWORK);

	k_sleep(K_MSEC(29800));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 3);
	k_sleep(K_MSEC(400));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 4);
	check_rejoin(3, 0, 0, true);
}

/* NWK stayed with the parent of the trust center rejoin, and the key comes late */
ZTEST(zzb_bdb_init, test_late_network_key_ends_the_pause)
{
	zassert_ok(zzb_bdb_start(node));
	fail_two_steps();
	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);
	k_sleep(K_MSEC(1800));
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NO_NETWORK);

	zzb_bdb_nwk_key_ind(node);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 1);
	zassert_equal(events.count, 2);
	zassert_equal(events.status, ZZB_BDB_SUCCESS);
	k_sleep(K_SECONDS(30));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 3);
}

static int64_t series_starts[8];
static int series_count;

/* Records the time of the first step of every series and fails every step */
static int fail_and_record(struct zzb_node *n, uint64_t ext_pan_id, enum zzb_nwk_join_method method,
			   uint32_t channels, uint8_t duration, uint8_t capability, bool secure)
{
	if (channels == 0 && series_count < ARRAY_SIZE(series_starts)) {
		series_starts[series_count++] = k_uptime_get();
	}

	return -ENETUNREACH;
}

/* 30 s, doubled after every failed series up to 15 min; NO_NETWORK is reported once */
ZTEST(zzb_bdb_init, test_pause_between_series)
{
	static const int pauses_s[] = {30, 60, 120, 240, 480, 900, 900};

	series_count = 0;
	zzb_nlme_join_req_fake.custom_fake = fail_and_record;
	zassert_ok(zzb_bdb_start(node));
	k_sleep(K_SECONDS(30 + 60 + 120 + 240 + 480 + 900 + 900 + 1));

	zassert_equal(series_count, ARRAY_SIZE(pauses_s) + 1);
	zassert_equal(zzb_nlme_join_req_fake.call_count, 3 * series_count);
	ARRAY_FOR_EACH(pauses_s, i) {
		zassert_within(series_starts[i + 1] - series_starts[i], pauses_s[i] * 1000, 20);
	}

	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NO_NETWORK);
	zassert_equal(zzb_bdb_start(node), -EBUSY);
}

ZTEST(zzb_bdb_init, test_parent_link_failure_starts_a_rejoin)
{
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE, 0x0000);
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);
	run_stack();
	check_rejoin(0, 0, 0, true);

	/* A second failure during the series changes nothing */
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE, 0x0000);
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);

	zzb_nlme_join_conf(node, ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 1);
	check_event(ZZB_BDB_EVENT_REJOIN, ZZB_BDB_SUCCESS);
}

/* R22 3.6.1.9.3: an end device rejoins to get a new address */
ZTEST(zzb_bdb_init, test_address_conflict_starts_a_rejoin)
{
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_ADDR_CONFLICT, NODE_ADDR + 1);
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);

	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_ADDR_CONFLICT, NODE_ADDR);
	run_stack();
	check_rejoin(0, 0, 0, true);
}

ZTEST(zzb_bdb_init, test_parent_link_failure_off_a_network)
{
	reboot(false);
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE, 0x0000);
	run_stack();
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);
}

ZTEST_SUITE(zzb_bdb_init, NULL, setup, before, NULL, NULL);
