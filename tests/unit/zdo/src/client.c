/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"

#define NODE_DESC_REQ 0x0002
#define TC_ADDR       0x0000

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node();
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

/*
 * Node_Desc_rsp of a trust center: primary trust center and network manager, stack compliance
 * revision 21
 */
static void receive_node_desc_rsp(uint8_t tsn, uint16_t src_addr, uint8_t status, size_t len)
{
	/* TSN, status, NWKAddrOfInterest, node descriptor */
	uint8_t rsp[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x8f, 0x00, 0x00,
			 0x50, 0xa0, 0x00, 0x41, 0x2a, 0x80, 0x00, 0x00};
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.src_addr = src_addr,
		.cluster = NODE_DESC_REQ | 0x8000,
	};

	rsp[0] = tsn;
	rsp[1] = status;
	receive_ind(&ind, rsp, len);
}

/* BDB 10.2.5 step 3 */
ZTEST(zzb_zdo_client, test_node_desc_req)
{
	uint8_t tsn = node->zdo.tsn;

	zassert_ok(zzb_zdo_node_desc_req(node, TC_ADDR));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_addr, TC_ADDR);
	zassert_equal(sent[0].req.cluster, NODE_DESC_REQ);
	zassert_equal(sent[0].req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(sent[0].len, 3);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){tsn, 0x00, 0x00}), 3);
	zassert_equal(node->zdo.tsn, (uint8_t)(tsn + 1));

	receive_node_desc_rsp(tsn, TC_ADDR, 0x00, 17);
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, 1);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg1_val, 0x00);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg2_val, TC_ADDR);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg3_val, 0x2a41);

	/* The transaction has ended */
	receive_node_desc_rsp(tsn, TC_ADDR, 0x00, 17);
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, 1);
}

/* A response matches by TSN, cluster and source */
ZTEST(zzb_zdo_client, test_unmatched_responses_and_timeout)
{
	uint8_t tsn = node->zdo.tsn;

	zassert_ok(zzb_zdo_node_desc_req(node, TC_ADDR));
	receive_node_desc_rsp(tsn + 1, TC_ADDR, 0x00, 17);
	receive_node_desc_rsp(tsn, 0x1234, 0x00, 17);
	/* A short descriptor */
	receive_node_desc_rsp(tsn, TC_ADDR, 0x00, 16);

	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, 1);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg1_val, 0x85);
}

ZTEST(zzb_zdo_client, test_error_status)
{
	uint8_t tsn = node->zdo.tsn;

	zassert_ok(zzb_zdo_node_desc_req(node, 0x1234));
	receive_node_desc_rsp(tsn, 0x1234, 0x80, 4);
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, 1);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg1_val, 0x80);
	zassert_equal(zzb_zdo_node_desc_conf_fake.arg2_val, 0x1234);
}

ZTEST(zzb_zdo_client, test_rejected_requests)
{
	for (int i = 0; i < CONFIG_ZZB_ZDO_TRANSACTION_COUNT; i++) {
		zassert_ok(zzb_zdo_node_desc_req(node, TC_ADDR));
	}

	zassert_equal(zzb_zdo_node_desc_req(node, TC_ADDR), -ENOBUFS);
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_zdo_node_desc_conf_fake.call_count, CONFIG_ZZB_ZDO_TRANSACTION_COUNT);

	/* An error of APSDE-DATA.request takes no entry */
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	for (int i = 0; i <= CONFIG_ZZB_ZDO_TRANSACTION_COUNT; i++) {
		zassert_equal(zzb_zdo_node_desc_req(node, TC_ADDR), -ENOTCONN);
	}
}

ZTEST_SUITE(zzb_zdo_client, NULL, setup, before, after, NULL);
