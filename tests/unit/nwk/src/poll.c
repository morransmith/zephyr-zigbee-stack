/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

/* Node 0 is the sleepy child of node 1 */
static struct zzb_node *child;
static struct zzb_node *parent;

static void *setup(void)
{
	child = test_node(0);
	parent = test_node(1);
	return NULL;
}

static void before(void *fixture)
{
	delete_nwk_records();
	reset_fakes();
	pair_nodes(child, parent);
	child->sleepy = true;
	zzb_nwk_poll_start(child);
}

static void after(void *fixture)
{
	zzb_nwk_reset(child);
	child->sleepy = false;
	check_no_buffer_leak();
}

static void receive_data(void)
{
	static const uint8_t nsdu[] = {0x42};
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
	};

	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
}

ZTEST(zzb_nwk_poll, test_sleepy_child_polls_at_the_long_interval)
{
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS - 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zassert_equal(poll_coord.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(poll_coord.pan_id, PAN_ID);
	zassert_equal(poll_coord.short_addr, PARENT_SHORT);

	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
}

ZTEST(zzb_nwk_poll, test_child_that_is_not_sleepy_does_not_poll)
{
	child->sleepy = false;
	pair_nodes(child, parent);
	zzb_nwk_poll_start(child);
	zassert_false(k_work_delayable_is_pending(&child->nwk.poll_timer));

	k_sleep(K_MSEC(2 * CONFIG_ZZB_NWK_LONG_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
}

/* The parent has more frames for the child */
ZTEST(zzb_nwk_poll, test_frame_pending_polls_at_once)
{
	receive_flags = ZZB_FRAME_PENDING;
	receive_data();
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);
	run_stack();
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);

	receive_flags = 0;
	receive_data();
	run_stack();
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS - 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
}

ZTEST(zzb_nwk_poll, test_no_poll_during_a_rejoin)
{
	zassert_ok(zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_REJOIN, 0, 0,
				     ZZB_NWK_CAP_RX_ON_IDLE | ZZB_NWK_CAP_ALLOC_ADDR, true));
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS + 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);

	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);
	zassert_equal(zzb_nlme_join_conf_fake.arg1_val, ZZB_NWK_STATUS_NO_NETWORKS);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
}

ZTEST(zzb_nwk_poll, test_reset_stops_polling)
{
	zassert_ok(zzb_nlme_reset_req(child));
	zassert_false(k_work_delayable_is_pending(&child->nwk.poll_timer));

	k_sleep(K_MSEC(2 * CONFIG_ZZB_NWK_LONG_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
}

ZTEST(zzb_nwk_poll, test_reboot_resumes_polling)
{
	zzb_nwk_save(child);
	zassert_ok(nwk_reboot());
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS + 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zassert_equal(poll_coord.short_addr, PARENT_SHORT);
}

ZTEST(zzb_nwk_poll, test_intervals_change_the_period)
{
	zzb_nwk_set_poll_intervals(child, 2000, 500);
	k_sleep(K_MSEC(1900));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
}

ZTEST(zzb_nwk_poll, test_hold_polls_at_the_short_interval)
{
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_APS, true);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_SHORT_POLL_MS - 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_SHORT_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
}

/* The poll armed at the short interval comes first, then the long interval applies */
ZTEST(zzb_nwk_poll, test_release_returns_to_the_long_interval)
{
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_APS, true);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_SHORT_POLL_MS + 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);

	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_APS, false);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_SHORT_POLL_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS - 200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 3);
}

ZTEST(zzb_nwk_poll, test_another_hold_keeps_the_short_interval)
{
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_APS, true);
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_ZDO, true);
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_APS, false);
	k_sleep(K_MSEC(3 * CONFIG_ZZB_NWK_SHORT_POLL_MS + 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 3);
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_ZDO, false);
}

ZTEST(zzb_nwk_poll, test_fast_poll_lasts_its_time)
{
	zzb_nwk_poll_fast(child);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_FAST_POLL_MS + 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count,
		      CONFIG_ZZB_NWK_FAST_POLL_MS / CONFIG_ZZB_NWK_SHORT_POLL_MS);

	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS - 300));
	zassert_equal(zzb_mlme_poll_req_fake.call_count,
		      CONFIG_ZZB_NWK_FAST_POLL_MS / CONFIG_ZZB_NWK_SHORT_POLL_MS);
	k_sleep(K_MSEC(400));
	zassert_equal(zzb_mlme_poll_req_fake.call_count,
		      CONFIG_ZZB_NWK_FAST_POLL_MS / CONFIG_ZZB_NWK_SHORT_POLL_MS + 1);
}

ZTEST(zzb_nwk_poll, test_holds_of_a_node_that_is_not_sleepy_arm_no_timer)
{
	child->sleepy = false;
	pair_nodes(child, parent);
	zzb_nwk_poll_start(child);
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_BDB, true);
	zzb_nwk_poll_fast(child);
	zassert_equal(child->nwk.poll_holds, ZZB_NWK_HOLD_BDB);
	zassert_false(k_work_delayable_is_pending(&child->nwk.poll_timer));
	zzb_nwk_poll_hold(child, ZZB_NWK_HOLD_BDB, false);
}

ZTEST_SUITE(zzb_nwk_poll, NULL, setup, before, after, NULL);
