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

#define OTHER_EUI64 0x0102030405060708ULL

static void receive_data(uint64_t dst_ext)
{
	static const uint8_t nsdu[] = {0x42};
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_DST_IEEE,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
		.dst_ext = dst_ext,
	};

	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
}

/* R22 3.6.1.9.2 and 3.6.1.9.3 */
ZTEST(zzb_nwk_status, test_address_conflict_is_reported)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	receive_data(OTHER_EUI64);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);

	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
	zassert_equal(sent[0].meta.dst.short_addr, PARENT_SHORT);
	zassert_equal(sent[0].meta.flags, 0);
	parse_sent_cmd(0, parent, &hdr, &cmd);
	zassert_equal(hdr.flags, ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE);
	zassert_equal(hdr.dst, 0xfffd);
	zassert_equal(hdr.src, CHILD_SHORT);
	zassert_equal(hdr.radius, ZZB_NWK_DEFAULT_RADIUS);
	zassert_equal(cmd.id, ZZB_NWK_CMD_NWK_STATUS);
	zassert_equal(cmd.nwk_status.code, 0x0d);
	zassert_true(cmd.nwk_status.has_addr);
	zassert_equal(cmd.nwk_status.addr, CHILD_SHORT);

	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x0d);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, CHILD_SHORT);
}

ZTEST(zzb_nwk_status, test_matching_or_unchecked_addresses_pass)
{
	receive_data(child->eui64);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);

	/* Off a network */
	zzb_nlme_set_ext_pan_id(child, 0);
	receive_data(OTHER_EUI64);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 2);
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

static void receive_rejoin_rsp(uint16_t short_addr, uint8_t status)
{
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
		.dst_ext = child->eui64,
		.src_ext = parent->eui64,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_REJOIN_RSP,
		.rejoin_rsp = {.short_addr = short_addr, .status = status},
	};

	receive_cmd(child, parent, &hdr, &cmd);
}

/* R22 3.6.1.9.3: the parent resolves a conflict with an unsolicited rejoin response */
ZTEST(zzb_nwk_status, test_network_address_update)
{
	receive_rejoin_rsp(0x5678, 0x01);
	receive_rejoin_rsp(0xfff8, 0x00);
	zassert_equal(child->nwk.nib.short_addr, CHILD_SHORT);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);

	receive_rejoin_rsp(0x5678, 0x00);
	zassert_equal(child->nwk.nib.short_addr, 0x5678);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0x5678);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x10);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, 0x5678);
}

ZTEST(zzb_nwk_status, test_rejoin_response_from_another_router_is_ignored)
{
	parent->nwk.nib.short_addr = 0x2222;
	receive_rejoin_rsp(0x5678, 0x00);
	zassert_equal(child->nwk.nib.short_addr, CHILD_SHORT);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
}

#define HANDLE 5

static void fail_transmissions(int count)
{
	for (int i = 0; i < count; i++) {
		zzb_mcps_data_conf(child, HANDLE, ZZB_MAC_STATUS_NO_ACK);
	}
}

ZTEST(zzb_nwk_status, test_parent_link_failure_after_failed_transmissions)
{
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES - 1);
	zzb_mcps_data_conf(child, HANDLE, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);

	fail_transmissions(1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlme_nwk_status_ind_fake.arg0_val, child);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x09);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, PARENT_SHORT);

	/* The count starts again */
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES - 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
}

/* A delivered frame or poll resets the count */
ZTEST(zzb_nwk_status, test_successful_transmission_resets_the_count)
{
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES - 1);
	zzb_mcps_data_conf(child, 0xf0 | ZZB_NWK_CMD_ED_TIMEOUT_REQ, ZZB_MAC_STATUS_SUCCESS);
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES - 1);
	zzb_mlme_poll_conf(child, ZZB_MAC_STATUS_NO_DATA);
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES - 2);
	zzb_mlme_poll_conf(child, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);

	zzb_mlme_poll_conf(child, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
}

/* A rejoin tries other routers, and a node without a parent has nothing to lose */
ZTEST(zzb_nwk_status, test_failures_are_not_counted_without_a_parent_or_during_a_procedure)
{
	zassert_ok(zzb_nlme_network_discovery_req(child, BIT(15), 0));
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES);
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);

	zzb_nwk_neighbor_remove(zzb_nwk_neighbor_parent(child));
	fail_transmissions(CONFIG_ZZB_NWK_PARENT_FAILURES);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
}

ZTEST_SUITE(zzb_nwk_status, NULL, setup, before, after, NULL);
