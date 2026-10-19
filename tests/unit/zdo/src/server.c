/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "fakes.h"

#define NWK_ADDR_REQ  0x0000
#define IEEE_ADDR_REQ 0x0001

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

ZTEST_SUITE(zzb_zdo_server, NULL, setup, before, after, NULL);
