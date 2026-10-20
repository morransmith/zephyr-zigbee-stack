/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "fakes.h"
#include "mac/mac.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* bdbPrimaryChannelSet and bdbSecondaryChannelSet */
#define PRIMARY   0x02108800
#define SECONDARY 0x05ef7000

#define OTHER_EXT_PAN_ID (EXT_PAN_ID + 1)

static struct zzb_node *node;

/* Descriptors of the last discovery, valid until the next one */
static struct zzb_nwk_network networks[2];

static void *setup(void)
{
	node = test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	reboot(false);
	networks[0] = (struct zzb_nwk_network){.ext_pan_id = OTHER_EXT_PAN_ID, .permit_join = true};
	networks[1] = (struct zzb_nwk_network){.ext_pan_id = EXT_PAN_ID, .permit_join = true};
}

static void check_event(int count, enum zzb_bdb_status status)
{
	zassert_equal(events.count, count);
	zassert_equal(events.event, ZZB_BDB_EVENT_STEERING);
	zassert_equal(events.status, status);
}

static void check_discovery(int i, uint32_t channels)
{
	zassert_equal(zzb_nlme_network_discovery_req_fake.call_count, i + 1);
	zassert_equal(zzb_nlme_network_discovery_req_fake.arg1_history[i], channels);
	zassert_equal(zzb_nlme_network_discovery_req_fake.arg2_history[i], 3);
}

/* Checks NLME-JOIN.request i: MAC association without security (BDB 8.2 step 7) */
static void check_join(int i, uint64_t ext_pan_id)
{
	zassert_equal(zzb_nlme_join_req_fake.call_count, i + 1);
	zassert_equal(zzb_nlme_join_req_fake.arg1_history[i], ext_pan_id);
	zassert_equal(zzb_nlme_join_req_fake.arg2_history[i], ZZB_NWK_JOIN_ASSOC);
	zassert_equal(zzb_nlme_join_req_fake.arg3_history[i], 0);
	zassert_equal(zzb_nlme_join_req_fake.arg4_history[i], 0);
	zassert_equal(zzb_nlme_join_req_fake.arg5_history[i], CAPABILITY);
	zassert_false(zzb_nlme_join_req_fake.arg6_history[i]);
}

/* bdbcMinCommissioningTime with TC significance (BDB 8.1 step 3, 8.2 step 14) */
static void check_permit_joining(int count)
{
	zassert_equal(zzb_zdo_mgmt_permit_joining_req_fake.call_count, count);
	zassert_equal(zzb_zdo_mgmt_permit_joining_req_fake.arg1_val, 180);
	zassert_true(zzb_zdo_mgmt_permit_joining_req_fake.arg2_val);
}

static void discovered(size_t count)
{
	zzb_nlme_network_discovery_conf(node, ZZB_MAC_STATUS_SUCCESS, networks, count);
}

/* The network key, then Node_Desc_rsp of a trust center of revision 20, which ends step 12 */
static void receive_key(void)
{
	zzb_bdb_nwk_key_ind(node);
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 1);
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_SUCCESS, 0x0000, 0x0000);
}

/* BDB 8.2 steps 2 to 16 */
ZTEST(zzb_bdb_steering, test_steering_joins_an_open_network)
{
	networks[0].permit_join = false;
	zassert_ok(zzb_bdb_steering(node));
	check_discovery(0, PRIMARY);
	zassert_equal(zzb_bdb_steering(node), -EBUSY);

	discovered(2);
	check_join(0, EXT_PAN_ID);
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(events.count, 0);

	receive_key();
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 1);
	check_permit_joining(1);
	check_event(1, ZZB_BDB_SUCCESS);
}

/* Section 3: the power source is set independently of the sleepy flag */
ZTEST(zzb_bdb_steering, test_sleepy_node_joins_with_the_receiver_off)
{
	node->sleepy = true;
	zassert_ok(zzb_bdb_steering(node));
	discovered(1);
	node->sleepy = false;
	zassert_equal(zzb_nlme_join_req_fake.arg5_val, SLEEPY_CAPABILITY);
}

/* BDB 8.1: an end device only extends the permit joining of the routers */
ZTEST(zzb_bdb_steering, test_steering_on_a_network)
{
	reboot(true);
	zzb_zdo_mgmt_permit_joining_req_fake.return_val = -ENOBUFS;
	zassert_ok(zzb_bdb_steering(node));
	check_permit_joining(1);
	check_event(1, ZZB_BDB_SUCCESS);
	zassert_equal(zzb_nlme_network_discovery_req_fake.call_count, 0);
}

ZTEST(zzb_bdb_steering, test_failed_join_tries_the_next_network)
{
	zassert_ok(zzb_bdb_steering(node));
	discovered(2);
	check_join(0, OTHER_EXT_PAN_ID);
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_NO_ACK);
	check_join(1, EXT_PAN_ID);
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_SUCCESS);
	receive_key();
	check_event(1, ZZB_BDB_SUCCESS);
}

/* NWK finds no parent of the first network among the beacons */
ZTEST(zzb_bdb_steering, test_refused_join_request_tries_the_next_network)
{
	static int results[] = {-ENETUNREACH, 0};

	SET_RETURN_SEQ(zzb_nlme_join_req, results, ARRAY_SIZE(results));
	zassert_ok(zzb_bdb_steering(node));
	discovered(2);
	check_join(1, EXT_PAN_ID);
}

/* BDB 8.2 steps 13 and 17 */
ZTEST(zzb_bdb_steering, test_no_network_on_either_channel_set)
{
	zassert_ok(zzb_bdb_steering(node));
	zzb_nlme_network_discovery_conf(node, ZZB_MAC_STATUS_NO_BEACON, NULL, 0);
	check_discovery(1, SECONDARY);

	networks[0].permit_join = false;
	discovered(1);
	zassert_equal(zzb_nlme_join_req_fake.call_count, 0);
	check_event(1, ZZB_BDB_NO_NETWORK);
}

/*
 * BDB 8.2 step 10: the next suitable network of the same channels. A sleepy node fetches the key by
 * polls (BDB 6.8).
 */
ZTEST(zzb_bdb_steering, test_network_key_does_not_arrive)
{
	zassert_ok(zzb_bdb_steering(node));
	discovered(2);
	check_join(0, OTHER_EXT_PAN_ID);
	zassert_false(held());
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_SUCCESS);
	zassert_true(held());

	k_sleep(K_MSEC(1600));
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 1);
	zassert_equal(zzb_aps_reset_fake.call_count, 1);
	check_discovery(1, PRIMARY);
	zassert_false(held());

	/* The network that failed is skipped */
	discovered(2);
	check_join(1, EXT_PAN_ID);
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_SUCCESS);
	receive_key();
	check_event(1, ZZB_BDB_SUCCESS);
	zassert_false(held());
}

/* The MAC refuses a scan while its TX FIFO holds frames */
ZTEST(zzb_bdb_steering, test_busy_discovery_is_repeated)
{
	zzb_nlme_network_discovery_req_fake.return_val = -EBUSY;
	zassert_ok(zzb_bdb_steering(node));
	k_sleep(K_MSEC(900));
	zassert_equal(zzb_nlme_network_discovery_req_fake.call_count, 1);
	zzb_nlme_network_discovery_req_fake.return_val = 0;
	k_sleep(K_MSEC(200));
	check_discovery(1, PRIMARY);
	zassert_equal(events.count, 0);
}

/* A network key update on the network does not end a procedure */
ZTEST(zzb_bdb_steering, test_network_key_outside_steering_is_ignored)
{
	zzb_bdb_nwk_key_ind(node);
	zassert_equal(zzb_zdo_device_annce_req_fake.call_count, 0);
	zassert_equal(events.count, 0);
}

ZTEST_SUITE(zzb_bdb_steering, NULL, setup, before, NULL, NULL);
