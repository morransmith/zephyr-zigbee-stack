/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"

#define MGMT_LQI_REQ            0x0031
#define MGMT_BIND_REQ           0x0033
#define MGMT_LEAVE_REQ          0x0034
#define MGMT_PERMIT_JOINING_REQ 0x0036
#define MGMT_NWK_UPDATE_REQ     0x0038

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	memset(node->nwk.neighbors, 0, sizeof(node->nwk.neighbors));
	memset(node->aps.aib.bindings, 0, sizeof(node->aps.aib.bindings));
	zzb_nlme_get_ext_pan_id_fake.return_val = 0x0102030405060708ULL;
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static struct zzb_nwk_neighbor *add_neighbor(int i, enum zzb_nwk_relationship relationship)
{
	struct zzb_nwk_neighbor *nb = &node->nwk.neighbors[i];

	*nb = (struct zzb_nwk_neighbor){
		.in_use = true,
		.ext_addr = 0x00124b0000000010ULL + i,
		.short_addr = 0x1110 + i,
		.device_type = ZZB_NWK_DEVICE_ROUTER,
		.relationship = relationship,
		.lqi = 200,
		.beacon.depth = 1,
	};
	return nb;
}

/* R22 Table 2-130 */
ZTEST(zzb_zdo_mgmt, test_mgmt_lqi)
{
	static const uint8_t req[] = {0x40, 0x00};
	static const uint8_t rsp[] = {
		0x40, 0x00, 0x01, 0x00, 0x01,
		/* Extended PAN ID, extended address, network address */
		0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x4b,
		0x12, 0x00, 0x10, 0x11,
		/* Router, receiver on, parent; permit joining unknown; depth, LQI */
		0x05, 0x02, 0x01, 0xc8};

	add_neighbor(0, ZZB_NWK_REL_PARENT);
	/* An entry from a beacon is not a neighbor of the network */
	add_neighbor(1, ZZB_NWK_REL_NONE);

	receive(MGMT_LQI_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_LQI_REQ | 0x8000, rsp, sizeof(rsp));

	receive(MGMT_LQI_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* As many records from the start index as fit in an ASDU (R22 2.4.4.4.2) */
ZTEST(zzb_zdo_mgmt, test_mgmt_lqi_pages)
{
	uint8_t req[] = {0x41, 0x00};

	for (int i = 0; i < 5; i++) {
		add_neighbor(i, ZZB_NWK_REL_CHILD);
	}

	receive(MGMT_LQI_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(sent[0].len, 5 + 3 * 22);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x41, 0x00, 0x05, 0x00, 0x03}), 5);

	req[1] = 0x03;
	receive(MGMT_LQI_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(sent[1].len, 5 + 2 * 22);
	zassert_mem_equal(sent[1].data, ((uint8_t[]){0x41, 0x00, 0x05, 0x03, 0x02}), 5);
	zassert_equal(sent[1].data[5 + 16], 0x13);

	req[1] = 0x05;
	receive(MGMT_LQI_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, MGMT_LQI_REQ | 0x8000, (uint8_t[]){0x41, 0x00, 0x05, 0x05, 0x00}, 5);
}

/* R22 Table 2-134 */
ZTEST(zzb_zdo_mgmt, test_mgmt_bind)
{
	static const uint8_t req[] = {0x42, 0x00};
	static const uint8_t rsp[] = {
		0x42, 0x00, 0x02, 0x00, 0x02,
		/* To an IEEE address and endpoint */
		0x04, 0x03, 0x02, 0x01, 0x00, 0x4b, 0x12, 0x00, 0x01, 0x06, 0x00, 0x03, 0x01, 0x02,
		0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x02,
		/* To a group */
		0x04, 0x03, 0x02, 0x01, 0x00, 0x4b, 0x12, 0x00, 0x01, 0x08, 0x00, 0x01, 0x34, 0x12};

	node->aps.aib.bindings[0] = (struct zzb_aps_binding){
		.in_use = true,
		.src_ep = 0x01,
		.cluster = 0x0006,
		.dst_mode = ZZB_APS_ADDR_EXT,
		.dst_ext = 0x0807060504030201ULL,
		.dst_ep = 0x02,
	};
	node->aps.aib.bindings[2] = (struct zzb_aps_binding){
		.in_use = true,
		.src_ep = 0x01,
		.cluster = 0x0008,
		.dst_mode = ZZB_APS_ADDR_GROUP,
		.dst_addr = 0x1234,
	};

	receive(MGMT_BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_BIND_REQ | 0x8000, rsp, sizeof(rsp));
}

ZTEST(zzb_zdo_mgmt, test_mgmt_bind_pages)
{
	static const uint8_t req[] = {0x43, 0x00};

	receive(MGMT_BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_BIND_REQ | 0x8000, (uint8_t[]){0x43, 0x00, 0x00, 0x00, 0x00}, 5);

	for (int i = 0; i < 4; i++) {
		node->aps.aib.bindings[i] = (struct zzb_aps_binding){
			.in_use = true,
			.src_ep = 0x01,
			.cluster = i,
			.dst_mode = ZZB_APS_ADDR_EXT,
		};
	}

	receive(MGMT_BIND_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(sent[1].len, 5 + 3 * 21);
	zassert_mem_equal(sent[1].data, ((uint8_t[]){0x43, 0x00, 0x04, 0x00, 0x03}), 5);
}

/* An end device does not permit joining (R22 3.2.2.7.3) */
ZTEST(zzb_zdo_mgmt, test_mgmt_permit_joining)
{
	static const uint8_t req[] = {0x44, 0xfe, 0x01};

	receive(MGMT_PERMIT_JOINING_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_PERMIT_JOINING_REQ | 0x8000, (uint8_t[]){0x44, 0xc2}, 2);

	receive(MGMT_PERMIT_JOINING_REQ, 0xfffc, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* R22 2.4.3.3.5: the response goes out before the node leaves */
ZTEST(zzb_zdo_mgmt, test_mgmt_leave)
{
	uint8_t req[] = {0x45, 0x04, 0x03, 0x02, 0x01, 0x00, 0x4b, 0x12, 0x00, 0x80};

	receive(MGMT_LEAVE_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.cluster, MGMT_LEAVE_REQ | 0x8000);
	zassert_equal(sent[0].handle, 0xfe);
	zassert_equal(sent[0].len, 2);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x45, 0x00}), 2);
	zassert_equal(zzb_bdb_leave_fake.call_count, 0);

	/* The rejoin flag of the request, as in R23 */
	zzb_apsde_data_conf(node, 0xff, ZZB_APS_STATUS_SUCCESS);
	zzb_apsde_data_conf(node, 0xfe, ZZB_APS_STATUS_NO_ACK);
	zassert_equal(zzb_bdb_leave_fake.call_count, 1);
	zassert_true(zzb_bdb_leave_fake.arg1_val);
	zassert_true(node->zdo.leave_rejoin);
	zzb_apsde_data_conf(node, 0xfe, ZZB_APS_STATUS_SUCCESS);
	zassert_equal(zzb_bdb_leave_fake.call_count, 1);

	/* The address 0 means the node; a response that cannot be sent does not stop the leave */
	memset(req + 1, 0, 9);
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOBUFS;
	receive(MGMT_LEAVE_REQ, NODE_ADDR, req, sizeof(req));
	zassert_equal(zzb_bdb_leave_fake.call_count, 2);
	zassert_false(zzb_bdb_leave_fake.arg1_val);
}

ZTEST(zzb_zdo_mgmt, test_mgmt_leave_of_another_device)
{
	static const uint8_t req[] = {0x46, 1, 2, 3, 4, 5, 6, 7, 8, 0x00};

	receive(MGMT_LEAVE_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	receive(MGMT_LEAVE_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_LEAVE_REQ | 0x8000, (uint8_t[]){0x46, 0xc8}, 2);
	zassert_equal(zzb_bdb_leave_fake.call_count, 0);
}

/* R22 Annex E: the node switches after nwkNetworkBroadcastDeliveryTime, 9 s */
ZTEST(zzb_zdo_mgmt, test_channel_change)
{
	/* Channel 15, scan duration 0xfe, nwkUpdateId 7 */
	uint8_t req[] = {0x47, 0x00, 0x80, 0x00, 0x00, 0xfe, 0x07};

	receive(MGMT_NWK_UPDATE_REQ, 0xfffd, req, sizeof(req));
	k_sleep(K_MSEC(8900));
	zassert_equal(zzb_nlme_set_channel_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_nlme_set_channel_fake.call_count, 1);
	zassert_equal(zzb_nlme_set_channel_fake.arg1_val, 15);
	/* Copied from the request, as in R23 Annex E */
	zassert_equal(zzb_nlme_set_channel_fake.arg2_val, 7);

	/* A new request replaces the pending one: channel 26 */
	receive(MGMT_NWK_UPDATE_REQ, 0xfffd, req, sizeof(req));
	k_sleep(K_MSEC(5000));
	req[2] = 0x00;
	req[4] = 0x04;
	req[6] = 0x08;
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, req, sizeof(req));
	k_sleep(K_MSEC(9100));
	zassert_equal(zzb_nlme_set_channel_fake.call_count, 2);
	zassert_equal(zzb_nlme_set_channel_fake.arg1_val, 26);
	zassert_equal(zzb_nlme_set_channel_fake.arg2_val, 8);

	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* R22 2.4.3.3.9: errors answer only a unicast request */
ZTEST(zzb_zdo_mgmt, test_rejected_mgmt_nwk_update)
{
	/* Channels 15 and 16 */
	uint8_t req[] = {0x48, 0x00, 0x80, 0x01, 0x00, 0xfe, 0x07};
	/* Scan duration 0xff with nwkUpdateId and nwkManagerAddr */
	static const uint8_t manager[] = {0x48, 0x00, 0x80, 0x00, 0x00, 0xff, 0x07, 0x00, 0x00};

	receive(MGMT_NWK_UPDATE_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MGMT_NWK_UPDATE_REQ | 0x8000, (uint8_t[]){0x48, 0xc2}, 2);

	/* Channel page 1 */
	req[2] = 0x00;
	req[3] = 0x00;
	req[4] = 0x08;
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, MGMT_NWK_UPDATE_REQ | 0x8000, (uint8_t[]){0x48, 0xc2}, 2);

	/* The MAC has no energy scan */
	req[5] = 0x03;
	req[6] = 0x01;
	receive(MGMT_NWK_UPDATE_REQ, 0xfffd, req, sizeof(req));
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, MGMT_NWK_UPDATE_REQ | 0x8000, (uint8_t[]){0x48, 0x84}, 2);

	req[5] = 0x06;
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, req, sizeof(req));
	check_response(3, MGMT_NWK_UPDATE_REQ | 0x8000, (uint8_t[]){0x48, 0xc2}, 2);

	/* apsChannelMaskList and nwkManagerAddr are not kept */
	receive(MGMT_NWK_UPDATE_REQ, NODE_ADDR, manager, sizeof(manager));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);

	k_sleep(K_MSEC(9100));
	zassert_equal(zzb_nlme_set_channel_fake.call_count, 0);
}

ZTEST_SUITE(zzb_zdo_mgmt, NULL, setup, before, after, NULL);
