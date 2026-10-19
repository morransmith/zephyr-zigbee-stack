/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "fakes.h"

#define NWK_ADDR_REQ  0x0000
#define IEEE_ADDR_REQ 0x0001
#define DEVICE_ANNCE  0x0013
#define BIND_REQ      0x0021
#define UNBIND_REQ    0x0022

/* EUI-64 and network address of the node, little-endian */
#define NODE_EUI64_LE 0x04, 0x03, 0x02, 0x01, 0x00, 0x4b, 0x12, 0x00
#define NODE_ADDR_LE  0x78, 0x56

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

/* R22 2.4.3.1.1; an end device has no associated devices (R22 Table 2-92) */
ZTEST(zzb_zdo_server, test_nwk_addr_req)
{
	uint8_t req[] = {0x10, NODE_EUI64_LE, 0x00, 0x00};
	static const uint8_t rsp[] = {0x10, 0x00, NODE_EUI64_LE, NODE_ADDR_LE, 0x00};
	static const uint8_t inv[] = {0x10, 0x80, NODE_EUI64_LE, NODE_ADDR_LE};

	receive(NWK_ADDR_REQ, 0xfffd, req, sizeof(req));
	check_response(0, NWK_ADDR_REQ | 0x8000, rsp, sizeof(rsp) - 1);

	req[9] = 0x01;
	receive(NWK_ADDR_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, NWK_ADDR_REQ | 0x8000, rsp, sizeof(rsp));

	/* Reserved request type */
	req[9] = 0x02;
	receive(NWK_ADDR_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	receive(NWK_ADDR_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, NWK_ADDR_REQ | 0x8000, inv, sizeof(inv));

	receive(NWK_ADDR_REQ, NODE_ADDR, req, sizeof(req) - 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
}

ZTEST(zzb_zdo_server, test_nwk_addr_req_for_another_device)
{
	static const uint8_t req[] = {0x11, 1, 2, 3, 4, 5, 6, 7, 8, 0x00, 0x00};
	static const uint8_t rsp[] = {0x11, 0x81, 1, 2, 3, 4, 5, 6, 7, 8, 0xff, 0xff};

	receive(NWK_ADDR_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	receive(NWK_ADDR_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, NWK_ADDR_REQ | 0x8000, rsp, sizeof(rsp));
}

/* R22 2.4.3.1.2 */
ZTEST(zzb_zdo_server, test_ieee_addr_req)
{
	uint8_t req[] = {0x12, NODE_ADDR_LE, 0x00, 0x00};
	static const uint8_t rsp[] = {0x12, 0x00, NODE_EUI64_LE, NODE_ADDR_LE};
	static const uint8_t not_found[] = {0x12, 0x81, 0xff, 0xff, 0xff, 0xff,
					    0xff, 0xff, 0xff, 0xff, 0x34, 0x12};

	receive(IEEE_ADDR_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, IEEE_ADDR_REQ | 0x8000, rsp, sizeof(rsp));

	req[1] = 0x34;
	req[2] = 0x12;
	receive(IEEE_ADDR_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	receive(IEEE_ADDR_REQ, 0x1234, req, sizeof(req));
	check_response(1, IEEE_ADDR_REQ | 0x8000, not_found, sizeof(not_found));
}

static int no_room(struct zzb_node *node, const struct zzb_aps_binding *arg)
{
	return -ENOMEM;
}

/* R22 2.4.3.2.2: ZHA binds clusters of the device to the coordinator */
ZTEST(zzb_zdo_server, test_bind_req)
{
	static const uint8_t req[] = {
		0x30, NODE_EUI64_LE, 0x01, 0x06, 0x00, 0x03, 1, 2, 3, 4, 5, 6, 7, 8, 0x02};

	receive(BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, BIND_REQ | 0x8000, (uint8_t[]){0x30, 0x00}, 2);
	zassert_equal(binding.src_ep, 0x01);
	zassert_equal(binding.cluster, 0x0006);
	zassert_equal(binding.dst_mode, ZZB_APS_ADDR_EXT);
	zassert_equal(binding.dst_ext, 0x0807060504030201ULL);
	zassert_equal(binding.dst_ep, 0x02);

	zzb_apsme_bind_req_fake.custom_fake = no_room;
	receive(BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, BIND_REQ | 0x8000, (uint8_t[]){0x30, 0x8c}, 2);

	receive(BIND_REQ, 0xfffd, req, sizeof(req));
	receive(BIND_REQ, NODE_ADDR, req, sizeof(req) - 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

ZTEST(zzb_zdo_server, test_unbind_req)
{
	static const uint8_t req[] = {0x31, NODE_EUI64_LE, 0x01, 0x06, 0x00, 0x01, 0x34, 0x12};

	receive(UNBIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, UNBIND_REQ | 0x8000, (uint8_t[]){0x31, 0x00}, 2);
	zassert_equal(binding.dst_mode, ZZB_APS_ADDR_GROUP);
	zassert_equal(binding.dst_addr, 0x1234);

	zzb_apsme_unbind_req_fake.custom_fake = NULL;
	zzb_apsme_unbind_req_fake.return_val = -ENOENT;
	receive(UNBIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, UNBIND_REQ | 0x8000, (uint8_t[]){0x31, 0x88}, 2);
}

/* R22 2.4.4.3.2: the binding table holds bindings of the node */
ZTEST(zzb_zdo_server, test_rejected_bind_req)
{
	uint8_t req[] = {0x32, NODE_EUI64_LE, 0x01, 0x06, 0x00, 0x01, 0x34, 0x12};

	req[1] = 0x05;
	receive(BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, BIND_REQ | 0x8000, (uint8_t[]){0x32, 0x84}, 2);

	req[1] = 0x04;
	req[9] = 0x00;
	receive(BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, BIND_REQ | 0x8000, (uint8_t[]){0x32, 0x82}, 2);

	req[9] = 0x01;
	req[12] = ZZB_APS_ADDR_SHORT;
	receive(BIND_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, BIND_REQ | 0x8000, (uint8_t[]){0x32, 0x84}, 2);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 0);
}

/* R22 2.5.4.5.5: Device_annce updates the nwkAddressMap entry of a known IEEE address */
ZTEST(zzb_zdo_server, test_device_annce)
{
	static const uint8_t annce[] = {0x46, 0x22, 0x44, 1, 2, 3, 4, 5, 6, 7, 8, 0x80};

	receive(DEVICE_ANNCE, 0xfffd, annce, sizeof(annce));
	zassert_equal(zzb_nlme_set_addr_map_fake.call_count, 0);

	zzb_nwk_addr_map_find_fake.return_val = 0;
	receive(DEVICE_ANNCE, 0xfffd, annce, sizeof(annce));
	zassert_equal(zzb_nlme_set_addr_map_fake.call_count, 1);
	zassert_equal(zzb_nlme_set_addr_map_fake.arg1_val, 0x0807060504030201ULL);
	zassert_equal(zzb_nlme_set_addr_map_fake.arg2_val, 0x4422);

	/* Without the capability octet */
	receive(DEVICE_ANNCE, 0xfffd, annce, sizeof(annce) - 1);
	zassert_equal(zzb_nlme_set_addr_map_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

ZTEST_SUITE(zzb_zdo_server, NULL, setup, before, after, NULL);
