/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

/* Node 0 is the child of node 1 */
static struct zzb_node *child;
static struct zzb_node *parent;

static void *setup(void)
{
	child = test_node(0);
	parent = test_node(1);
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	pair_nodes(child, parent);
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static struct zzb_nwk_hdr cmd_hdr(uint16_t dst, uint16_t flags)
{
	return (struct zzb_nwk_hdr){
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SRC_IEEE | flags,
		.dst = dst,
		.src = PARENT_SHORT,
		.radius = 1,
		.src_ext = parent->eui64,
	};
}

ZTEST(zzb_nwk_status, test_network_status_is_indicated)
{
	struct zzb_nwk_hdr hdr = cmd_hdr(CHILD_SHORT, ZZB_NWK_SECURITY);
	struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_NWK_STATUS,
		.nwk_status = {.code = 0x0d, .has_addr = true, .addr = 0x5678},
	};

	receive_cmd(child, parent, &hdr, &cmd);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlme_nwk_status_ind_fake.arg0_val, child);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x0d);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, 0x5678);

	/* Without an address in the payload the indication has the NWK source */
	hdr.dst = 0xfffd;
	cmd.nwk_status.code = 0x0f;
	cmd.nwk_status.has_addr = false;
	receive_cmd(child, parent, &hdr, &cmd);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 2);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x0f);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, PARENT_SHORT);
}

ZTEST(zzb_nwk_status, test_unsecured_or_other_commands_are_dropped)
{
	const struct zzb_nwk_hdr hdr = cmd_hdr(CHILD_SHORT, 0);
	const struct zzb_nwk_hdr secured = cmd_hdr(CHILD_SHORT, ZZB_NWK_SECURITY);
	const struct zzb_nwk_cmd status = {
		.id = ZZB_NWK_CMD_NWK_STATUS,
		.nwk_status = {.code = 0x0d, .has_addr = true, .addr = 0x5678},
	};
	const struct zzb_nwk_cmd route_req = {
		.id = ZZB_NWK_CMD_ROUTE_REQ,
		.route_req = {.id = 1, .dst = 0x5678},
	};
	static const uint8_t truncated[] = {ZZB_NWK_CMD_NWK_STATUS};

	receive_cmd(child, parent, &hdr, &status);
	receive_cmd(child, parent, &secured, &route_req);
	receive_frame(child, parent, &secured, truncated, sizeof(truncated));
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
}

ZTEST_SUITE(zzb_nwk_status, NULL, setup, before, after, NULL);
