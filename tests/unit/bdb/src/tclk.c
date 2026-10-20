/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "fakes.h"
#include "mac/mac.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* Server mask with stack compliance revision 22 and 0 (Z-Stack Home 1.2) */
#define SERVER_MASK_R22 0x2c00
#define SERVER_MASK_R0  0x0001

static const uint8_t key_b[ZZB_SEC_KEY_LEN] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node();
	return NULL;
}

/* Steering up to BDB 8.2 step 11: the network key has arrived */
static void before(void *fixture)
{
	static struct zzb_nwk_network network = {.ext_pan_id = EXT_PAN_ID, .permit_join = true};

	reset_fakes();
	reboot(false);
	zassert_ok(zzb_bdb_steering(node));
	zzb_nlme_network_discovery_conf(node, ZZB_MAC_STATUS_SUCCESS, &network, 1);
	zzb_nlme_join_conf(node, ZZB_MAC_STATUS_SUCCESS);
	zzb_bdb_nwk_key_ind(node);
}

static void check_steering(enum zzb_bdb_status status)
{
	zassert_equal(events.count, 1);
	zassert_equal(events.event, ZZB_BDB_EVENT_STEERING);
	zassert_equal(events.status, status);
}

/* BDB 8.2 step 12: a leave without rejoin, then the reset */
static void check_failure(void)
{
	zassert_equal(events.count, 0);
	zassert_equal(zzb_nlme_leave_req_fake.call_count, 1);
	zassert_false(zzb_nlme_leave_req_fake.arg1_val);
	zassert_equal(zzb_zdo_mgmt_permit_joining_req_fake.call_count, 0);

	zzb_nlme_leave_conf(node, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_nlme_reset_req_fake.call_count, 1);
	zassert_equal(zzb_aps_reset_fake.call_count, 1);
	check_steering(ZZB_BDB_TCLK_EX_FAILURE);
}

static void receive_node_desc(uint16_t server_mask)
{
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_SUCCESS, 0x0000, server_mask);
}

/* Steps 2 to 13 */
ZTEST(zzb_bdb_tclk, test_trust_center_link_key_exchange)
{
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 1);
	zassert_equal(zzb_zdo_node_desc_req_fake.arg1_val, 0x0000);
	zassert_true(held());
	receive_node_desc(SERVER_MASK_R22);

	zassert_true(held());
	zassert_equal(zzb_apsme_request_key_req_fake.call_count, 1);
	zassert_equal(zzb_apsme_request_key_req_fake.arg1_val, TC_EUI64);
	zzb_bdb_tc_link_key_ind(node, TC_EUI64, key_b);
	zassert_equal(set_link_key.count, 1);
	zassert_equal(set_link_key.addr, TC_EUI64);
	zassert_mem_equal(set_link_key.key, key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(set_link_key.attr, ZZB_APS_KEY_UNVERIFIED);
	zassert_equal(set_link_key.type, ZZB_APS_LINK_KEY_UNIQUE);

	zassert_equal(zzb_apsme_verify_key_req_fake.call_count, 1);
	zassert_equal(zzb_apsme_verify_key_req_fake.arg1_val, TC_EUI64);
	zassert_equal(zzb_apsme_verify_key_req_fake.arg2_val, ZZB_APS_KEY_TC_LINK);
	zassert_equal(events.count, 0);
	zassert_true(held());
	zzb_apsme_confirm_key_ind(node, ZZB_APS_STATUS_SUCCESS, TC_EUI64, ZZB_APS_KEY_TC_LINK);
	zassert_equal(zzb_zdo_mgmt_permit_joining_req_fake.call_count, 1);
	check_steering(ZZB_BDB_SUCCESS);
	zassert_false(held());

	/* A repeated Confirm Key belongs to no procedure */
	zzb_apsme_confirm_key_ind(node, ZZB_APS_STATUS_SUCCESS, TC_EUI64, ZZB_APS_KEY_TC_LINK);
	zassert_equal(events.count, 1);
}

/* Step 5 */
ZTEST(zzb_bdb_tclk, test_trust_center_before_r21)
{
	receive_node_desc(SERVER_MASK_R0);
	zassert_equal(zzb_apsme_request_key_req_fake.call_count, 0);
	check_steering(ZZB_BDB_SUCCESS);
}

/* Step 4: the confirm of the ZDO client is the timer */
ZTEST(zzb_bdb_tclk, test_node_desc_attempts)
{
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_TIMEOUT, 0x0000, 0);
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_NOT_SUPPORTED, 0x0000, 0);
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 3);
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_TIMEOUT, 0x0000, 0);
	check_failure();
}

/* A request that is not sent counts as an attempt and gets the BDB timer */
ZTEST(zzb_bdb_tclk, test_node_desc_request_not_sent)
{
	zzb_zdo_node_desc_req_fake.return_val = -ENOBUFS;
	zzb_zdo_node_desc_conf(node, ZZB_ZDP_TIMEOUT, 0x0000, 0);
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 2);

	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 2);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_zdo_node_desc_req_fake.call_count, 3);
	k_sleep(K_SECONDS(5));
	check_failure();
}

/* Steps 7 and 8: bdbcTCLinkKeyExchangeTimeout per attempt */
ZTEST(zzb_bdb_tclk, test_request_key_attempts)
{
	receive_node_desc(SERVER_MASK_R22);
	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_apsme_request_key_req_fake.call_count, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsme_request_key_req_fake.call_count, 2);
	k_sleep(K_SECONDS(5));
	zassert_equal(zzb_apsme_request_key_req_fake.call_count, 3);
	zassert_equal(zzb_nlme_leave_req_fake.call_count, 0);
	k_sleep(K_SECONDS(5));
	check_failure();
}

/* Step 9 */
ZTEST(zzb_bdb_tclk, test_same_key_fails)
{
	receive_node_desc(SERVER_MASK_R22);
	zzb_bdb_tc_link_key_ind(node, TC_EUI64, tc_pair.key);
	zassert_equal(set_link_key.count, 0);
	check_failure();
}

/* Steps 11 and 12 */
ZTEST(zzb_bdb_tclk, test_verify_key_attempts)
{
	receive_node_desc(SERVER_MASK_R22);
	zzb_bdb_tc_link_key_ind(node, TC_EUI64, key_b);
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_apsme_verify_key_req_fake.call_count, 2);
	k_sleep(K_SECONDS(5));
	zassert_equal(zzb_apsme_verify_key_req_fake.call_count, 3);
	k_sleep(K_SECONDS(5));
	check_failure();
}

/* BDB 10.2.6: acceptNewUnsolicitedTrustCenterLinkKey is FALSE */
ZTEST(zzb_bdb_tclk, test_unsolicited_trust_center_link_key)
{
	receive_node_desc(SERVER_MASK_R0);
	zzb_bdb_tc_link_key_ind(node, TC_EUI64, key_b);
	zassert_equal(set_link_key.count, 0);
}

ZTEST_SUITE(zzb_bdb_tclk, NULL, setup, before, NULL, NULL);
