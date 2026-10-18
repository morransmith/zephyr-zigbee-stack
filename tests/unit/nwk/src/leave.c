/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
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

/* Checks the leave command in sent frame i and confirms it with status */
static void check_leave(int i, uint8_t options, enum zzb_mac_status status)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	zassert_equal(zzb_mcps_data_req_fake.call_count, i + 1);
	zassert_equal(sent[i].meta.dst.short_addr, PARENT_SHORT);
	zassert_equal(sent[i].meta.flags, ZZB_FRAME_ACK_REQ, "R22 3.4.4.1");
	zassert_equal(sent[i].meta.handle, 0xf0 | ZZB_NWK_CMD_LEAVE);

	parse_sent_cmd(i, parent, &hdr, &cmd);
	zassert_equal(hdr.type, ZZB_NWK_FRAME_CMD);
	zassert_equal(hdr.flags, ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE);
	zassert_equal(hdr.dst, 0xfffd);
	zassert_equal(hdr.src, CHILD_SHORT);
	zassert_equal(hdr.radius, 1);
	zassert_equal(hdr.src_ext, child->eui64);
	zassert_equal(cmd.id, ZZB_NWK_CMD_LEAVE);
	zassert_equal(cmd.leave_options, options);

	zzb_mcps_data_conf(child, sent[i].meta.handle, status);
}

static void check_reset(void)
{
	const struct zzb_nib *nib = &child->nwk.nib;

	zassert_equal(nib->pan_id, 0xffff);
	zassert_equal(nib->short_addr, 0xffff);
	zassert_equal(nib->ext_pan_id, 0);
	zassert_false(nib->keys[0].valid);
	zassert_false(nib->keys[1].valid);
	zassert_is_null(zzb_nwk_neighbor_parent(child));
	zassert_false(k_work_delayable_is_pending(&child->nwk.keepalive));
	zassert_equal(zzb_mlme_set_pan_id_fake.arg1_val, 0xffff);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0xffff);
}

/* R22 3.6.1.10.1 and 3.6.1.10.4 */
ZTEST(zzb_nwk_leave, test_leave_without_rejoin)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	uint32_t fc = child->nwk.fc.value;
	uint8_t seq;

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);
	zzb_nwk_ed_timeout_start(child);

	zassert_ok(zzb_nlme_leave_req(child, false));
	seq = child->nwk.nib.seq;
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true), -EBUSY);
	net_buf_unref(buf);

	check_leave(0, 0, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_nlme_leave_conf_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlme_leave_conf_fake.arg0_val, child);
	zassert_equal(zzb_nlme_leave_conf_fake.arg1_val, ZZB_MAC_STATUS_SUCCESS);
	check_reset();

	/* The outgoing frame counter survives the leave (R22 4.3.4) */
	zassert_equal(child->nwk.fc.value, fc + 1);
	zassert_equal(child->nwk.nib.seq, seq);
	zassert_equal(zzb_nlme_leave_req(child, false), -ENOTCONN);
}

/* The rejoin that follows needs the network */
ZTEST(zzb_nwk_leave, test_leave_with_rejoin_keeps_the_network)
{
	zassert_ok(zzb_nlme_leave_req(child, true));
	check_leave(0, ZZB_NWK_LEAVE_REJOIN, ZZB_MAC_STATUS_NO_ACK);

	zassert_equal(zzb_nlme_leave_conf_fake.call_count, 1);
	zassert_equal(zzb_nlme_leave_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(child->nwk.nib.ext_pan_id, EXT_PAN_ID);
	zassert_equal(child->nwk.nib.short_addr, CHILD_SHORT);
	zassert_true(child->nwk.nib.keys[0].valid);
	zassert_not_null(zzb_nwk_neighbor_parent(child));
}

ZTEST(zzb_nwk_leave, test_leave_rejected)
{
	zassert_ok(zzb_nlme_network_discovery_req(child, BIT(15), 0));
	zassert_equal(zzb_nlme_leave_req(child, false), -EBUSY);
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);

	/* Without a parent, without a network, and without a key */
	zzb_nwk_neighbor_remove(zzb_nwk_neighbor_parent(child));
	zassert_equal(zzb_nlme_leave_req(child, false), -ENOTCONN);

	pair_nodes(child, parent);
	zzb_nlme_set_ext_pan_id(child, 0);
	zassert_equal(zzb_nlme_leave_req(child, false), -ENOTCONN);

	zzb_nwk_init(child);
	zassert_ok(zzb_nwk_fc_start(child));
	zzb_nlme_set_ext_pan_id(child, EXT_PAN_ID);
	zzb_nlme_set_short_addr(child, CHILD_SHORT);
	zzb_nwk_neighbor_add(child, parent->eui64, PARENT_SHORT, ZZB_NWK_REL_PARENT);
	zassert_equal(zzb_nlme_leave_req(child, false), -ENOENT);

	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
	zassert_equal(child->nwk.op, ZZB_NWK_OP_NONE);
}

/* BDB 8.2 step 10: a join without the network key ends without a leave command */
ZTEST(zzb_nwk_leave, test_reset)
{
	child->nwk.key_pending = true;
	zzb_nwk_ed_timeout_start(child);

	zassert_ok(zzb_nlme_reset_req(child));
	check_reset();
	zassert_false(child->nwk.key_pending);
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);

	zassert_ok(zzb_nlme_network_discovery_req(child, BIT(15), 0));
	zassert_equal(zzb_nlme_reset_req(child), -EBUSY);
	zzb_mlme_scan_conf(child, ZZB_MAC_STATUS_NO_BEACON);
}

ZTEST_SUITE(zzb_nwk_leave, NULL, setup, before, after, NULL);
