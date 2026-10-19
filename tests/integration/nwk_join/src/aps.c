/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "child.h"
#include "common/buf.h"
#include "common/node.h"
#include "parent.h"

/* On/Off command Toggle to the trust center */
static void send_data(uint8_t tx_options)
{
	static const uint8_t asdu[] = {0x01, 0x42, 0x02};
	const struct zzb_apsde_req req = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = PARENT_ADDR,
		.dst_ep = 0x01,
		.profile = 0x0104,
		.cluster = 0x0006,
		.src_ep = 0x01,
		.tx_options = tx_options,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_APSDE_HEADROOM);
	net_buf_add_mem(buf, asdu, sizeof(asdu));
	zassert_ok(zzb_apsde_data_req(child, buf, &req));
}

static void *setup(void)
{
	child_init();
	return NULL;
}

static void before(void *fixture)
{
	child_join();
}

/* BDB 10.2.5 steps 7 to 12 */
ZTEST(zzb_aps_join, test_trust_center_link_key_exchange)
{
	struct zzb_aps_key_pair *pair;

	zassert_ok(zzb_apsme_request_key_req(child, PARENT_EUI64));
	k_sleep(K_MSEC(10));
	zassert_equal(parent_log.aps_cmd.id, ZZB_APS_CMD_REQUEST_KEY);
	zassert_equal(child_log.transport_keys, 2);
	pair = zzb_aps_key_pair_find(child, PARENT_EUI64);
	zassert_mem_equal(pair->key, tc_link_key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_UNVERIFIED);

	zassert_ok(zzb_apsme_verify_key_req(child, PARENT_EUI64, ZZB_APS_KEY_TC_LINK));
	k_sleep(K_MSEC(10));
	zassert_equal(parent_log.aps_cmd.id, ZZB_APS_CMD_VERIFY_KEY);
	zassert_equal(child_log.confirm_keys, 1);
	zassert_equal(child_log.confirm_status, ZZB_APS_STATUS_SUCCESS);
	zassert_equal(pair->attr, ZZB_APS_KEY_VERIFIED);
}

ZTEST(zzb_aps_join, test_acknowledged_data)
{
	send_data(ZZB_APS_TX_ACK);
	k_sleep(K_MSEC(10));

	zassert_equal(parent_log.aps_data, 1);
	zassert_equal(child_log.data_confs, 1);
	zassert_equal(child_log.data_status, ZZB_APS_STATUS_SUCCESS);
}

/* R22 2.2.8.4.4: the same APS counter, a higher frame counter */
ZTEST(zzb_aps_join, test_retransmission)
{
	uint8_t counter;
	uint32_t fc;

	parent_withhold_aps_acks(1);
	send_data(ZZB_APS_TX_ACK | ZZB_APS_TX_SECURITY);
	k_sleep(K_MSEC(10));
	zassert_equal(parent_log.aps_data, 1);
	zassert_equal(child_log.data_confs, 0);
	counter = parent_log.aps_hdr.counter;
	fc = parent_log.aps_fc;

	k_sleep(K_MSEC(1700));
	zassert_equal(parent_log.aps_data, 2);
	zassert_equal(parent_log.aps_hdr.counter, counter);
	zassert_true(parent_log.aps_fc > fc);
	zassert_equal(child_log.data_confs, 1);
	zassert_equal(child_log.data_status, ZZB_APS_STATUS_SUCCESS);
}

/* The first ACK of the child is lost, so the trust center repeats the frame */
ZTEST(zzb_aps_join, test_duplicate)
{
	parent_send_aps_data(0x40);
	k_sleep(K_MSEC(10));
	zassert_equal(child_log.data_inds, 1);
	zassert_equal(parent_log.aps_acks, 1);

	parent_send_aps_data(0x40);
	k_sleep(K_MSEC(10));
	zassert_equal(child_log.data_inds, 1);
	zassert_equal(parent_log.aps_acks, 2);
}

/* R22 4.6.3.4.2: the next frames of the child use network key 1 */
ZTEST(zzb_aps_join, test_network_key_switch)
{
	const struct zzb_nib *nib = &child->nwk.nib;

	parent_send_nwk_key_update();
	k_sleep(K_MSEC(10));
	zassert_equal(child_log.transport_keys, 2);
	zassert_equal(nib->keys[nib->active_key].seq, 1);

	send_data(0);
	k_sleep(K_MSEC(10));
	zassert_equal(parent_log.aps_data, 1);
	zassert_equal(parent_log.nwk_key_seq, 1);
}

ZTEST_SUITE(zzb_aps_join, NULL, setup, before, NULL, NULL);
