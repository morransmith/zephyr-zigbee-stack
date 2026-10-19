/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"

#define MGMT_LQI_REQ            0x0031
#define MGMT_BIND_REQ           0x0033
#define MGMT_PERMIT_JOINING_REQ 0x0036

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

ZTEST_SUITE(zzb_zdo_mgmt, NULL, setup, before, after, NULL);
