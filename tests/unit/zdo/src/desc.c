/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "fakes.h"

#define NODE_DESC_REQ  0x0002
#define POWER_DESC_REQ 0x0003

#define NODE_ADDR_LE 0x78, 0x56

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

/* R22 2.3.2.3: stack compliance revision 22 in the server mask (BDB 10.2.5) */
ZTEST(zzb_zdo_desc, test_node_desc)
{
	static const uint8_t req[] = {0x20, NODE_ADDR_LE};
	static const uint8_t rsp[] = {0x20, 0x00, 0x78, 0x56, 0x02, 0x40, 0x88, 0x34, 0x12,
				      0x52, 0x52, 0x00, 0x00, 0x2c, 0x52, 0x00, 0x00};

	receive(NODE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, NODE_DESC_REQ | 0x8000, rsp, sizeof(rsp));
}

/* R22 2.4.4.2.3; the request is unicast only (R22 2.4.2.1) */
ZTEST(zzb_zdo_desc, test_node_desc_of_another_device)
{
	static const uint8_t req[] = {0x21, 0x34, 0x12};
	static const uint8_t rsp[] = {0x21, 0x80, 0x34, 0x12};

	receive(NODE_DESC_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	receive(NODE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, NODE_DESC_REQ | 0x8000, rsp, sizeof(rsp));
}

/* R22 2.3.2.4: receiver synchronized with RxOnWhenIdle, mains power, level 100 % */
ZTEST(zzb_zdo_desc, test_power_desc)
{
	static const uint8_t req[] = {0x22, NODE_ADDR_LE};
	static const uint8_t rsp[] = {0x22, 0x00, NODE_ADDR_LE, 0x10, 0xc1};

	receive(POWER_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, POWER_DESC_REQ | 0x8000, rsp, sizeof(rsp));
}

ZTEST_SUITE(zzb_zdo_desc, NULL, setup, before, after, NULL);
