/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "fakes.h"

#define NODE_DESC_REQ   0x0002
#define POWER_DESC_REQ  0x0003
#define SIMPLE_DESC_REQ 0x0004
#define ACTIVE_EP_REQ   0x0005
#define MATCH_DESC_REQ  0x0006

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

/* R22 2.3.2.5 */
ZTEST(zzb_zdo_desc, test_simple_desc)
{
	uint8_t req[] = {0x23, NODE_ADDR_LE, 0x01};
	/* Status, address, length, endpoint, profile, device, version, clusters */
	static const uint8_t light[] = {0x23, 0x00, 0x78, 0x56, 0x0e, 0x01, 0x04, 0x01, 0x00, 0x01,
					0x01, 0x03, 0x00, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00};
	static const uint8_t light_switch[] = {0x23, 0x00, 0x78, 0x56, 0x0a, 0x02, 0x04, 0x01,
					       0x03, 0x01, 0x00, 0x00, 0x01, 0x06, 0x00};

	receive(SIMPLE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, SIMPLE_DESC_REQ | 0x8000, light, sizeof(light));

	req[3] = 0x02;
	receive(SIMPLE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, SIMPLE_DESC_REQ | 0x8000, light_switch, sizeof(light_switch));

	receive(SIMPLE_DESC_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* R22 2.4.4.2.5: the range of the endpoint, the address, then the endpoint */
ZTEST(zzb_zdo_desc, test_simple_desc_errors)
{
	uint8_t req[] = {0x24, 0x34, 0x12, 0x00};

	receive(SIMPLE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, SIMPLE_DESC_REQ | 0x8000, (uint8_t[]){0x24, 0x82, 0x34, 0x12, 0x00}, 5);

	req[3] = 0x03;
	receive(SIMPLE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, SIMPLE_DESC_REQ | 0x8000, (uint8_t[]){0x24, 0x80, 0x34, 0x12, 0x00}, 5);

	req[1] = 0x78;
	req[2] = 0x56;
	receive(SIMPLE_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, SIMPLE_DESC_REQ | 0x8000, (uint8_t[]){0x24, 0x83, 0x78, 0x56, 0x00}, 5);
}

ZTEST(zzb_zdo_desc, test_active_ep)
{
	uint8_t req[] = {0x25, NODE_ADDR_LE};
	static const uint8_t rsp[] = {0x25, 0x00, NODE_ADDR_LE, 0x02, 0x01, 0x02};

	receive(ACTIVE_EP_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, ACTIVE_EP_REQ | 0x8000, rsp, sizeof(rsp));

	req[1] = 0x34;
	req[2] = 0x12;
	receive(ACTIVE_EP_REQ, NODE_ADDR, req, sizeof(req));
	check_response(1, ACTIVE_EP_REQ | 0x8000, (uint8_t[]){0x25, 0x80, 0x34, 0x12, 0x00}, 5);
}

/* R22 2.4.4.2.7: input clusters match servers, output clusters match clients */
ZTEST(zzb_zdo_desc, test_match_desc)
{
	uint8_t req[] = {0x26, 0xfd, 0xff, 0x04, 0x01, 0x01, 0x06, 0x00, 0x01, 0x06, 0x00};
	static const uint8_t both[] = {0x26, 0x00, NODE_ADDR_LE, 0x02, 0x01, 0x02};
	static const uint8_t light[] = {0x26, 0x00, NODE_ADDR_LE, 0x01, 0x01};
	static const uint8_t none[] = {0x26, 0x00, NODE_ADDR_LE, 0x00};

	receive(MATCH_DESC_REQ, 0xfffd, req, sizeof(req));
	check_response(0, MATCH_DESC_REQ | 0x8000, both, sizeof(both));

	/* Any profile, input clusters only */
	req[3] = 0xff;
	req[4] = 0xff;
	req[8] = 0x00;
	receive(MATCH_DESC_REQ, 0xfffd, req, sizeof(req) - 2);
	check_response(1, MATCH_DESC_REQ | 0x8000, light, sizeof(light));

	/* Another profile matches nothing; a broadcast gets no response then */
	req[3] = 0x09;
	req[4] = 0x01;
	req[8] = 0x01;
	receive(MATCH_DESC_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	req[1] = 0x78;
	req[2] = 0x56;
	receive(MATCH_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(2, MATCH_DESC_REQ | 0x8000, none, sizeof(none));

	/* The cluster lists exceed the payload */
	receive(MATCH_DESC_REQ, NODE_ADDR, req, sizeof(req) - 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
}

/* R22 2.4.4.2.7 step 2 */
ZTEST(zzb_zdo_desc, test_match_desc_for_another_device)
{
	static const uint8_t req[] = {0x27, 0x34, 0x12, 0x04, 0x01, 0x01, 0x06, 0x00, 0x00};

	receive(MATCH_DESC_REQ, 0xfffd, req, sizeof(req));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	receive(MATCH_DESC_REQ, NODE_ADDR, req, sizeof(req));
	check_response(0, MATCH_DESC_REQ | 0x8000, (uint8_t[]){0x27, 0x80, 0x34, 0x12, 0x00}, 5);
}

ZTEST_SUITE(zzb_zdo_desc, NULL, setup, before, after, NULL);
