/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

#define CHANNEL    15
#define CAPABILITY (ZZB_NWK_CAP_RX_ON_IDLE | ZZB_NWK_CAP_ALLOC_ADDR)
#define NEW_SHORT  0x2222

/* Superframe Specification with association permit */
#define SF_COORD  (0x0fff | ZZB_MAC_SF_PAN_COORD | ZZB_MAC_SF_ASSOC_PERMIT)
#define SF_ROUTER (0x0fff | ZZB_MAC_SF_ASSOC_PERMIT)

/* The parent of the test network has no capacity left */
static const struct zzb_nwk_beacon parent_beacon = {
	.stack_profile = 2,
	.protocol_version = 2,
	.ext_pan_id = EXT_PAN_ID,
	.tx_offset = 0xffffff,
};

static const struct zzb_nwk_beacon router_beacon = {
	.stack_profile = 2,
	.protocol_version = 2,
	.depth = 1,
	.ed_capacity = true,
	.ext_pan_id = EXT_PAN_ID,
	.tx_offset = 0xffffff,
};

/* Node 0 is the child of node 1 */
static struct zzb_node *child;
static struct zzb_node *parent;

static void *setup(void)
{
	child = test_node(0);
	parent = test_node(1);
	return NULL;
}

/* The parent entry as a join leaves it */
static void before(void *fixture)
{
	struct zzb_nwk_neighbor *nb;

	reset_fakes();
	pair_nodes(child, parent);
	child->nwk.nib.channel = CHANNEL;
	nb = zzb_nwk_neighbor_parent(child);
	nb->beacon = parent_beacon;
	nb->pan_id = PAN_ID;
	nb->channel = CHANNEL;
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static int rejoin(bool secure)
{
	return zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_REJOIN, 0, 0, CAPABILITY, secure);
}

static void end_scan(void)
{
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_SUCCESS);
}

/* Checks rejoin request i, sent to short_addr, and confirms it */
static void check_request(int i, uint16_t short_addr, uint64_t dst_ext, uint16_t src)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	zassert_equal(zzb_mcps_data_req_fake.call_count, i + 1);
	zassert_equal(sent[i].meta.dst.short_addr, short_addr);
	zassert_equal(sent[i].meta.flags, ZZB_FRAME_ACK_REQ);
	zassert_equal(sent[i].meta.handle, 0xf0 | ZZB_NWK_CMD_REJOIN_REQ);

	parse_sent_cmd(i, parent, &hdr, &cmd);
	zassert_equal(hdr.flags,
		      ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | (dst_ext ? ZZB_NWK_DST_IEEE : 0));
	zassert_equal(hdr.dst, short_addr);
	zassert_equal(hdr.src, src);
	zassert_equal(hdr.radius, 1);
	zassert_equal(hdr.src_ext, child->eui64);
	zassert_equal(hdr.dst_ext, dst_ext);
	zassert_equal(cmd.id, ZZB_NWK_CMD_REJOIN_REQ);
	/* Stochastic addressing: allocate address 0 (R22 3.6.1.4.2) */
	zassert_equal(cmd.capability, ZZB_NWK_CAP_RX_ON_IDLE);

	zzb_mcps_data_conf(child, sent[i].meta.handle, ZZB_MAC_STATUS_SUCCESS);
}

static void respond(uint16_t short_addr, uint8_t status, uint64_t dst_ext)
{
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE,
		.dst = child->nwk.nib.short_addr,
		.src = parent->nwk.nib.short_addr,
		.radius = 1,
		.dst_ext = dst_ext,
		.src_ext = parent->eui64,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_REJOIN_RSP,
		.rejoin_rsp = {.short_addr = short_addr, .status = status},
	};

	receive_cmd(child, parent, &hdr, &cmd);
}

static void check_conf(uint8_t status)
{
	zassert_equal(zzb_nlme_join_conf_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlme_join_conf_fake.arg0_val, child);
	zassert_equal(zzb_nlme_join_conf_fake.arg1_val, status);
}

ZTEST(zzb_nwk_rejoin, test_secure_rejoin_to_the_parent)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	child->nwk.nib.parent_info = 0x01;
	zassert_ok(rejoin(true));
	zassert_equal(child->nwk.nib.parent_info, 0);

	/* BDB 7.1 passes no channels: the channel of the network is scanned */
	zassert_equal(zzb_mlme_scan_req_fake.call_count, 1);
	zassert_equal(zzb_mlme_scan_req_fake.arg1_val, BIT(CHANNEL));
	notify_beacon(child, &parent_beacon, PAN_ID, PARENT_SHORT, CHANNEL, SF_COORD, 150);
	end_scan();

	zassert_equal(zzb_mlme_set_channel_fake.call_count, 1);
	zassert_equal(zzb_mlme_set_channel_fake.arg1_val, CHANNEL);
	check_request(0, PARENT_SHORT, parent->eui64, CHILD_SHORT);

	respond(CHILD_SHORT, 0x00, child->eui64);
	check_conf(ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(zzb_nwk_neighbor_parent(child)->ext_addr, parent->eui64);
	zassert_equal(child->nwk.op, ZZB_NWK_OP_NONE);

	/* The End Device Timeout Request follows (R22 3.6.10.2) */
	run_stack();
	zassert_equal(zzb_mcps_data_req_fake.call_count, 2);
	parse_sent_cmd(1, parent, &hdr, &cmd);
	zassert_equal(cmd.id, ZZB_NWK_CMD_ED_TIMEOUT_REQ);
}

/* The new parent is known only from its beacon until the secured response (R22 3.6.1.9.2) */
ZTEST(zzb_nwk_rejoin, test_secure_rejoin_to_a_new_parent)
{
	struct zzb_nwk_neighbor *nb;

	/* Node 1 plays another router; the old parent is not heard */
	zzb_nwk_neighbor_parent(child)->ext_addr = 0x0102030405060708ULL;
	parent->nwk.nib.short_addr = NEW_SHORT;

	zassert_ok(rejoin(true));
	notify_beacon(child, &router_beacon, PAN_ID, NEW_SHORT, CHANNEL, SF_ROUTER, 100);
	end_scan();
	check_request(0, NEW_SHORT, 0, CHILD_SHORT);

	respond(0x5678, 0x00, child->eui64);
	check_conf(ZZB_NWK_STATUS_SUCCESS);
	nb = zzb_nwk_neighbor_parent(child);
	zassert_equal(nb->short_addr, NEW_SHORT);
	zassert_equal(nb->ext_addr, parent->eui64);
	zassert_is_null(zzb_nwk_neighbor_find_ext(child, 0x0102030405060708ULL));
	zassert_equal(child->nwk.nib.short_addr, 0x5678);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0x5678);
}

ZTEST(zzb_nwk_rejoin, test_rejoin_tries_the_next_candidate)
{
	parent->nwk.nib.short_addr = NEW_SHORT;
	zassert_ok(rejoin(true));
	notify_beacon(child, &parent_beacon, PAN_ID, PARENT_SHORT, CHANNEL, SF_COORD, 150);
	notify_beacon(child, &router_beacon, PAN_ID + 1, NEW_SHORT, 20, SF_ROUTER, 100);
	end_scan();
	check_request(0, PARENT_SHORT, parent->eui64, CHILD_SHORT);

	/* A refusal from the parent, then no response from the router */
	parent->nwk.nib.short_addr = PARENT_SHORT;
	respond(0xffff, ZZB_MAC_STATUS_PAN_AT_CAPACITY, child->eui64);
	zassert_equal(zzb_mlme_set_channel_fake.arg1_val, 20);
	zassert_equal(zzb_mlme_set_pan_id_fake.arg1_val, PAN_ID + 1);
	check_request(1, NEW_SHORT, 0, CHILD_SHORT);
	zassert_equal(zzb_nlme_join_conf_fake.call_count, 0);

	/* macResponseWaitTime, 491.52 ms */
	k_sleep(K_MSEC(480));
	zassert_equal(zzb_nlme_join_conf_fake.call_count, 0);
	k_sleep(K_MSEC(20));
	check_conf(ZZB_NWK_STATUS_NOT_PERMITTED);

	/* The node is back on its PAN and channel */
	zassert_equal(child->nwk.nib.pan_id, PAN_ID);
	zassert_equal(zzb_mlme_set_pan_id_fake.arg1_val, PAN_ID);
	zassert_equal(zzb_mlme_set_channel_fake.arg1_val, CHANNEL);
	zassert_equal(zzb_nwk_neighbor_parent(child)->ext_addr, parent->eui64);
}

ZTEST(zzb_nwk_rejoin, test_failed_transmission_tries_the_next_candidate)
{
	zassert_ok(rejoin(true));
	notify_beacon(child, &parent_beacon, PAN_ID, PARENT_SHORT, CHANNEL, SF_COORD, 150);
	end_scan();

	zzb_mcps_data_conf(child, 0xf0 | ZZB_NWK_CMD_REJOIN_REQ, ZZB_MAC_STATUS_NO_ACK);
	check_conf(ZZB_NWK_STATUS_NOT_PERMITTED);
}

ZTEST(zzb_nwk_rejoin, test_rejoin_without_beacons)
{
	struct zzb_nwk_beacon other = router_beacon;

	other.ext_pan_id = EXT_PAN_ID + 1;
	zassert_ok(zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_REJOIN, BIT(11) | BIT(25), 2,
				     CAPABILITY, true));
	zassert_equal(zzb_mlme_scan_req_fake.arg1_val, BIT(11) | BIT(25));
	zassert_equal(zzb_mlme_scan_req_fake.arg2_val, 2);

	/* A beacon of another network */
	notify_beacon(child, &other, PAN_ID, NEW_SHORT, CHANNEL, SF_ROUTER, 100);
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_SUCCESS);
	check_conf(ZZB_NWK_STATUS_NOT_PERMITTED);

	RESET_FAKE(zzb_nlme_join_conf);
	zassert_ok(rejoin(true));
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);
	check_conf(ZZB_NWK_STATUS_NO_NETWORKS);
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

/* Without a valid network address the node picks one for the request (R22 3.6.1.4.2) */
ZTEST(zzb_nwk_rejoin, test_rejoin_selects_an_address)
{
	uint16_t addr;

	zzb_nlme_set_short_addr(child, 0xffff);
	zassert_ok(rejoin(true));
	notify_beacon(child, &parent_beacon, PAN_ID, PARENT_SHORT, CHANNEL, SF_COORD, 150);
	end_scan();

	addr = child->nwk.nib.short_addr;
	zassert_true(addr != 0x0000 && addr < 0xfff8);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, addr);
	check_request(0, PARENT_SHORT, parent->eui64, addr);

	k_sleep(K_MSEC(500));
	check_conf(ZZB_NWK_STATUS_NOT_PERMITTED);
	zassert_equal(child->nwk.nib.short_addr, 0xffff);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0xffff);
}

/* While a rejoin runs, only the rejoin response of the candidate is processed */
ZTEST(zzb_nwk_rejoin, test_other_frames_are_dropped)
{
	static const uint8_t nsdu[] = {0x42};
	const struct zzb_nwk_hdr data = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
	};
	const struct zzb_nwk_hdr status_hdr = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
		.src_ext = parent->eui64,
	};
	const struct zzb_nwk_cmd status = {
		.id = ZZB_NWK_CMD_NWK_STATUS,
		.nwk_status = {.code = 0x0d, .has_addr = true, .addr = 0x5678},
	};

	zassert_ok(rejoin(true));
	notify_beacon(child, &parent_beacon, PAN_ID, PARENT_SHORT, CHANNEL, SF_COORD, 150);
	end_scan();
	check_request(0, PARENT_SHORT, parent->eui64, CHILD_SHORT);

	receive_frame(child, parent, &data, nsdu, sizeof(nsdu));
	receive_cmd(child, parent, &status_hdr, &status);
	respond(CHILD_SHORT, 0x00, 0x0102030405060708ULL);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
	zassert_equal(zzb_nlme_join_conf_fake.call_count, 0);

	respond(CHILD_SHORT, 0x00, child->eui64);
	check_conf(ZZB_NWK_STATUS_SUCCESS);
}

ZTEST(zzb_nwk_rejoin, test_rejoin_request_rejected)
{
	zassert_equal(zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_REJOIN, 0, 0,
					CAPABILITY | ZZB_NWK_CAP_ROUTER, true),
		      -ENOTSUP);
	zassert_ok(rejoin(true));
	zassert_equal(rejoin(true), -EBUSY);
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);

	/* A secure rejoin needs the network key */
	zzb_nwk_init(child);
	zassert_equal(rejoin(true), -ENOENT);
}

ZTEST_SUITE(zzb_nwk_rejoin, NULL, setup, before, after, NULL);
