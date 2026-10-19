/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "fakes.h"

#define MGMT_RTG_REQ 0x0032
#define DEVICE_ANNCE 0x0013
#define PARENT_ANNCE 0x001f
#define TC_EUI64     0x00124b0000000001ULL

static const uint8_t key[ZZB_SEC_KEY_LEN] = {0x01, 0x02, 0x03};

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* R22 2.4.4.1: the response holds the TSN and the status */
ZTEST(zzb_zdo, test_unsupported_request)
{
	static const uint8_t req[] = {0x42, 0x00};
	static const uint8_t rsp[] = {0x42, 0x84};

	receive(MGMT_RTG_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_response(0, MGMT_RTG_REQ | 0x8000, rsp, sizeof(rsp));

	/* No error status answers a broadcast */
	receive(MGMT_RTG_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

ZTEST(zzb_zdo, test_dropped_frames)
{
	static const uint8_t req[] = {0x43, 0x00};
	struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = 0x01,
		.src_addr = REQUESTER_ADDR,
		.cluster = MGMT_RTG_REQ,
	};

	/* An application endpoint, until ZCL */
	receive_ind(&ind, req, sizeof(req));

	/* A ZDO endpoint frame of another profile */
	ind.dst_ep = 0x00;
	ind.profile = 0x0104;
	receive_ind(&ind, req, sizeof(req));

	/* Without a TSN */
	receive(MGMT_RTG_REQ, NODE_ADDR, req, 0);

	/* A response without a transaction */
	receive(MGMT_RTG_REQ | 0x8000, NODE_ADDR, req, sizeof(req));

	/* Announcements get no response (R22 2.4.3.1.11, 2.4.3.1.12) */
	receive(DEVICE_ANNCE, NODE_ADDR,
		(uint8_t[]){0x44, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8, 0x80}, 12);
	receive(PARENT_ANNCE, NODE_ADDR, (uint8_t[]){0x45, 0x00}, 2);

	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* The network key reaches NLME-SET through ZDO (section 5) */
ZTEST(zzb_zdo, test_network_key)
{
	zzb_apsme_transport_key_ind(test_node(), TC_EUI64, ZZB_APS_KEY_NWK, key, 3);
	zassert_equal(zzb_apsme_set_tc_addr_fake.call_count, 1);
	zassert_equal(zzb_apsme_set_tc_addr_fake.arg1_val, TC_EUI64);
	zassert_equal(set_nwk_key.count, 1);
	zassert_equal_ptr(set_nwk_key.key, key);
	zassert_equal(set_nwk_key.seq, 3);
	zassert_equal(zzb_bdb_nwk_key_ind_fake.call_count, 1);

	/* Without a key-pair entry for the trust center the key stays unset */
	zzb_apsme_set_tc_addr_fake.return_val = -ENOMEM;
	zzb_apsme_transport_key_ind(test_node(), TC_EUI64, ZZB_APS_KEY_NWK, key, 4);
	zassert_equal(set_nwk_key.count, 1);
	zassert_equal(zzb_bdb_nwk_key_ind_fake.call_count, 1);
}

/* BDB decides about a trust center link key (BDB 10.2.6) */
ZTEST(zzb_zdo, test_trust_center_link_key_is_dropped)
{
	zzb_apsme_transport_key_ind(test_node(), TC_EUI64, ZZB_APS_KEY_TC_LINK, key, 0);
	zassert_equal(zzb_apsme_set_tc_addr_fake.call_count, 0);
	zassert_equal(set_nwk_key.count, 0);
}

ZTEST(zzb_zdo, test_switch_key)
{
	zzb_apsme_switch_key_ind(test_node(), TC_EUI64, 3);
	zassert_equal(zzb_nlme_set_active_key_fake.call_count, 1);
	zassert_equal(zzb_nlme_set_active_key_fake.arg1_val, 3);
}

ZTEST_SUITE(zzb_zdo, NULL, setup, before, after, NULL);
