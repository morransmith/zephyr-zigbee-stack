/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"

#define IEEE_ADDR_REQ   0x0001
#define NODE_DESC_REQ   0x0002
#define SIMPLE_DESC_REQ 0x0004
#define DEVICE_ANNCE    0x0013
#define PERMIT_JOIN     0x0036
#define TC_ADDR         0x0000

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

/* R22 2.4.3.1.11: to the devices with the receiver on, without APS ACK */
ZTEST(zzb_zdo_client, test_device_annce)
{
	uint8_t tsn = node->zdo.tsn;

	zassert_ok(zzb_zdo_device_annce_req(node));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_addr, 0xfffd);
	zassert_equal(sent[0].req.cluster, DEVICE_ANNCE);
	zassert_equal(sent[0].req.tx_options, 0);
	zassert_equal(sent[0].len, 12);
	zassert_mem_equal(sent[0].data,
			  ((uint8_t[]){tsn, 0x78, 0x56, 0x04, 0x03, 0x02, 0x01, 0x00, 0x4b, 0x12,
				       0x00, 0x88}),
			  12);

	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	zassert_equal(zzb_zdo_device_annce_req(node), -ENOTCONN);
}

/* BDB 8.2 step 14: to all routers and the coordinator, without APS ACK */
ZTEST(zzb_zdo_client, test_mgmt_permit_joining_req)
{
	uint8_t tsn = node->zdo.tsn;

	zassert_ok(zzb_zdo_mgmt_permit_joining_req(node, 180, true));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_addr, 0xfffc);
	zassert_equal(sent[0].req.cluster, PERMIT_JOIN);
	zassert_equal(sent[0].req.tx_options, 0);
	zassert_equal(sent[0].len, 3);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){tsn, 180, 0x01}), 3);
	zassert_equal(node->zdo.tsn, (uint8_t)(tsn + 1));
}

static void receive_rsp(uint16_t cluster, uint16_t src_addr, const uint8_t *rsp, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.src_addr = src_addr,
		.cluster = cluster | 0x8000,
	};

	receive_ind(&ind, rsp, len);
}

/* BDB 8.5 steps 5 and 6 */
ZTEST(zzb_zdo_client, test_simple_desc_req)
{
	uint8_t tsn = node->zdo.tsn;
	/* Endpoint 1 of an On/Off Light: Identify and On/Off servers, OTA Upgrade client */
	uint8_t rsp[] = {tsn,  0x00, 0x34, 0x12, 0x0e, 0x01, 0x04, 0x01, 0x00, 0x01,
			 0x01, 0x02, 0x03, 0x00, 0x06, 0x00, 0x01, 0x19, 0x00};

	zassert_ok(zzb_zdo_simple_desc_req(node, 0x1234, 1));
	zassert_equal(sent[0].req.dst_addr, 0x1234);
	zassert_equal(sent[0].req.cluster, SIMPLE_DESC_REQ);
	zassert_equal(sent[0].req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(sent[0].len, 4);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){tsn, 0x34, 0x12, 0x01}), 4);

	/* A descriptor shorter than its length field is dropped */
	receive_rsp(SIMPLE_DESC_REQ, 0x1234, rsp, sizeof(rsp) - 1);
	zassert_equal(zzb_zdo_simple_desc_conf_fake.call_count, 0);

	receive_rsp(SIMPLE_DESC_REQ, 0x1234, rsp, sizeof(rsp));
	zassert_equal(zzb_zdo_simple_desc_conf_fake.call_count, 1);
	zassert_equal(zzb_zdo_simple_desc_conf_fake.arg1_val, 0x00);
	zassert_equal(zzb_zdo_simple_desc_conf_fake.arg2_val, 0x1234);
	zassert_equal(simple_desc.ep, 1);
	zassert_equal(simple_desc.profile, 0x0104);
	zassert_equal(simple_desc.device_id, 0x0100);
	zassert_equal(simple_desc.in_count, 2);
	zassert_mem_equal(simple_desc_in, ((uint8_t[]){0x03, 0x00, 0x06, 0x00}), 4);
	zassert_equal(simple_desc.out_count, 1);
	zassert_mem_equal(simple_desc_out, ((uint8_t[]){0x19, 0x00}), 2);

	/* NOT_ACTIVE without a descriptor */
	tsn = node->zdo.tsn;
	zassert_ok(zzb_zdo_simple_desc_req(node, 0x1234, 7));
	receive_rsp(SIMPLE_DESC_REQ, 0x1234, (uint8_t[]){tsn, 0x83, 0x34, 0x12, 0x00}, 5);
	zassert_equal(zzb_zdo_simple_desc_conf_fake.call_count, 2);
	zassert_equal(zzb_zdo_simple_desc_conf_fake.arg1_val, 0x83);
	zassert_is_null(zzb_zdo_simple_desc_conf_fake.arg3_val);
}

/* BDB 8.5 step 8: the IEEE address of a respondent goes to nwkAddressMap */
ZTEST(zzb_zdo_client, test_ieee_addr_req)
{
	uint8_t tsn = node->zdo.tsn;
	const uint8_t rsp[] = {tsn, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0x34, 0x12};

	zassert_ok(zzb_zdo_ieee_addr_req(node, 0x1234));
	zassert_equal(sent[0].req.dst_addr, 0x1234);
	zassert_equal(sent[0].req.cluster, IEEE_ADDR_REQ);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){tsn, 0x34, 0x12, 0x00, 0x00}), 5);

	receive_rsp(IEEE_ADDR_REQ, 0x1234, rsp, sizeof(rsp) - 1);
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.call_count, 0);
	receive_rsp(IEEE_ADDR_REQ, 0x1234, rsp, sizeof(rsp));
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.call_count, 1);
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.arg1_val, 0x00);
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.arg2_val, 0x1234);
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.arg3_val, 0x0807060504030201ULL);
	zassert_equal(zzb_nlme_set_addr_map_fake.call_count, 1);
	zassert_equal(zzb_nlme_set_addr_map_fake.arg1_val, 0x0807060504030201ULL);
	zassert_equal(zzb_nlme_set_addr_map_fake.arg2_val, 0x1234);

	/* No response */
	zassert_ok(zzb_zdo_ieee_addr_req(node, 0x2222));
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.call_count, 2);
	zassert_equal(zzb_zdo_ieee_addr_conf_fake.arg1_val, 0x85);
	zassert_equal(zzb_nlme_set_addr_map_fake.call_count, 1);
}

ZTEST_SUITE(zzb_zdo_client, NULL, setup, before, after, NULL);
