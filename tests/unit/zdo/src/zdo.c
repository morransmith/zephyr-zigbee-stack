/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "fakes.h"

#define MGMT_RTG_REQ 0x0032

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

	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

ZTEST_SUITE(zzb_zdo, NULL, setup, before, after, NULL);
