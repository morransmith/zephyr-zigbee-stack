/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"
#include "sec/ccm.h"

#define HANDLE 5

static const uint8_t nsdu[] = {0x40, 0x00, 0x06, 0x00, 0x04, 0x01, 0x01, 0x10};

/* Node 0 is the child of node 1 */
static struct zzb_node *child;
static struct zzb_node *parent;

static struct net_buf *nsdu_buf(void)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);
	net_buf_add_mem(buf, nsdu, sizeof(nsdu));
	zzb_frame_meta(buf)->handle = HANDLE;
	return buf;
}

static void check_mac_dst(int i, uint16_t short_addr, uint8_t flags)
{
	zassert_equal(sent[i].meta.dst.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(sent[i].meta.dst.pan_id, PAN_ID);
	zassert_equal(sent[i].meta.dst.short_addr, short_addr);
	zassert_equal(sent[i].meta.flags, flags);
	zassert_equal(sent[i].meta.handle, HANDLE);
}

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
	child->sleepy = false;
	check_no_buffer_leak();
}

ZTEST(zzb_nwk_data, test_secured_unicast_reaches_the_parent)
{
	uint8_t seq = child->nwk.nib.seq;
	struct zzb_nwk_hdr hdr;

	zassert_ok(zzb_nlde_data_req(child, nsdu_buf(), PARENT_SHORT, 0, true));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
	check_mac_dst(0, PARENT_SHORT, ZZB_FRAME_ACK_REQ);

	zassert_equal(zzb_nwk_hdr_parse(sent[0].data, sent[0].len, &hdr), 8);
	zassert_equal(hdr.type, ZZB_NWK_FRAME_DATA);
	zassert_equal(hdr.flags, ZZB_NWK_DISCOVER_ROUTE | ZZB_NWK_SECURITY);
	zassert_equal(hdr.dst, PARENT_SHORT);
	zassert_equal(hdr.src, CHILD_SHORT);
	zassert_equal(hdr.radius, ZZB_NWK_DEFAULT_RADIUS);
	zassert_equal(hdr.seq, seq);
	zassert_equal(child->nwk.nib.seq, (uint8_t)(seq + 1));
	zassert_equal(sent[0].len, 8 + ZZB_NWK_AUX_LEN + sizeof(nsdu) + ZZB_SEC_MIC_LEN);

	receive(parent, sent[0].data, sent[0].len, CHILD_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlde_data_ind_fake.arg0_val, parent);
	zassert_equal(zzb_nlde_data_ind_fake.arg2_val, PARENT_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.arg3_val, CHILD_SHORT);
	zassert_true(zzb_nlde_data_ind_fake.arg4_val);
	zassert_equal(data_ind.len, sizeof(nsdu));
	zassert_mem_equal(data_ind.data, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nwk_neighbor_find_short(parent, CHILD_SHORT)->lqi, 200);
}

/* With the 9-byte MAC header of a NWK frame the secured frame fills aMaxPHYPacketSize - FCS */
ZTEST(zzb_nwk_data, test_longest_nsdu_fits_the_buffer)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	const size_t len = 125 - 9 - 8 - ZZB_NWK_AUX_LEN - ZZB_SEC_MIC_LEN;

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);
	zassert_true(net_buf_tailroom(buf) >= len + ZZB_SEC_MIC_LEN);
	memset(net_buf_add(buf, len), 0x5a, len);
	zzb_frame_meta(buf)->handle = HANDLE;

	zassert_ok(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true));
	zassert_equal(sent[0].len, 125 - 9);
}

/* A neighbor is the next hop; the radius is the one requested */
ZTEST(zzb_nwk_data, test_unicast_to_a_neighbor)
{
	struct zzb_nwk_hdr hdr;

	zassert_ok(zzb_nlde_data_req(parent, nsdu_buf(), CHILD_SHORT, 1, false));
	check_mac_dst(0, CHILD_SHORT, ZZB_FRAME_ACK_REQ);
	zassert_equal(zzb_nwk_hdr_parse(sent[0].data, sent[0].len, &hdr), 8);
	zassert_equal(hdr.flags, ZZB_NWK_DISCOVER_ROUTE);
	zassert_equal(hdr.radius, 1);
	zassert_equal(sent[0].len, 8 + sizeof(nsdu));
}

/* R22 3.6.5: MAC unicast to the parent without ACK request */
ZTEST(zzb_nwk_data, test_broadcast_goes_to_the_parent)
{
	struct zzb_nwk_hdr hdr;

	zassert_ok(zzb_nlde_data_req(child, nsdu_buf(), 0xfffd, 0, true));
	check_mac_dst(0, PARENT_SHORT, 0);
	zassert_equal(zzb_nwk_hdr_parse(sent[0].data, sent[0].len, &hdr), 8);
	zassert_equal(hdr.flags, ZZB_NWK_SECURITY);
	zassert_equal(hdr.dst, 0xfffd);
}

ZTEST(zzb_nwk_data, test_request_rejected)
{
	struct net_buf *buf = nsdu_buf();

	zassert_equal(zzb_nlde_data_req(child, buf, 0xfffe, 0, true), -EINVAL);
	net_buf_unref(buf);

	/* Handles from 0xf0 belong to NWK command frames */
	buf = nsdu_buf();
	zzb_frame_meta(buf)->handle = 0xf0;
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true), -EINVAL);
	net_buf_unref(buf);

	/* The parent has no parent of its own */
	buf = nsdu_buf();
	zassert_equal(zzb_nlde_data_req(parent, buf, 0x5678, 0, true), -ENOTCONN);
	net_buf_unref(buf);

	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM - 1);
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true), -EMSGSIZE);
	net_buf_unref(buf);

	zzb_nlme_set_short_addr(child, 0xffff);
	buf = nsdu_buf();
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true), -ENOTCONN);
	net_buf_unref(buf);

	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

ZTEST(zzb_nwk_data, test_errors_are_returned)
{
	struct net_buf *buf = nsdu_buf();

	zzb_nwk_init(child);
	zzb_nlme_set_short_addr(child, CHILD_SHORT);
	zzb_nwk_neighbor_add(child, parent->eui64, PARENT_SHORT, ZZB_NWK_REL_PARENT);
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true), -ENOENT, "no key");
	net_buf_unref(buf);

	zzb_mcps_data_req_fake.custom_fake = NULL;
	zzb_mcps_data_req_fake.return_val = -EMSGSIZE;
	buf = nsdu_buf();
	zassert_equal(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, false), -EMSGSIZE);
	net_buf_unref(buf);
}

ZTEST(zzb_nwk_data, test_confirm)
{
	zzb_mcps_data_conf(child, HANDLE, ZZB_MAC_STATUS_NO_ACK);

	zassert_equal(zzb_nlde_data_conf_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlde_data_conf_fake.arg0_val, child);
	zassert_equal(zzb_nlde_data_conf_fake.arg1_val, HANDLE);
	zassert_equal(zzb_nlde_data_conf_fake.arg2_val, ZZB_MAC_STATUS_NO_ACK);

	/* The confirm of a NWK command frame stays in NWK */
	zzb_mcps_data_conf(child, 0xf0 | ZZB_NWK_CMD_ED_TIMEOUT_REQ, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_nlde_data_conf_fake.call_count, 1);
}

/* R22 3.6.2.2: APS takes the Transport Key from them */
ZTEST(zzb_nwk_data, test_unsecured_data_passes_while_the_key_is_pending)
{
	/* Data to 0x1234 and to 0xffff from 0x0000 */
	static const uint8_t unicast[] = {0x08, 0x00, 0x34, 0x12, 0x00, 0x00, 0x1e, 0x01, 0x42};
	static const uint8_t broadcast[] = {0x08, 0x00, 0xff, 0xff, 0x00, 0x00, 0x1e, 0x02, 0x42};

	receive(child, unicast, sizeof(unicast), PARENT_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);

	child->nwk.key_pending = true;
	receive(child, unicast, sizeof(unicast), PARENT_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);
	zassert_false(zzb_nlde_data_ind_fake.arg4_val);
	zassert_equal(data_ind.len, 1);
	zassert_equal(data_ind.data[0], 0x42);
	zassert_equal(data_ind.meta.lqi, 200);

	receive(child, broadcast, sizeof(broadcast), PARENT_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 2);
	zassert_equal(zzb_nlde_data_ind_fake.arg2_val, 0xffff);

	zzb_nlme_set_nwk_key(child, test_key, 1);
	receive(child, unicast, sizeof(unicast), PARENT_SHORT);
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 2);
}

ZTEST(zzb_nwk_data, test_frames_are_dropped)
{
	static const uint8_t truncated[] = {0x08, 0x00, 0x34};
	/* To another device, to routers only, from the node itself, and a leave command */
	static const uint8_t other_dst[] = {0x08, 0x00, 0x78, 0x56, 0x00, 0x00, 0x1e, 0x01, 0x42};
	static const uint8_t routers[] = {0x08, 0x00, 0xfc, 0xff, 0x00, 0x00, 0x1e, 0x02, 0x42};
	static const uint8_t own_src[] = {0x08, 0x00, 0xff, 0xff, 0x34, 0x12, 0x1e, 0x03, 0x42};
	static const uint8_t command[] = {0x09, 0x00, 0x34, 0x12, 0x00,
					  0x00, 0x01, 0x04, 0x04, 0x00};

	receive(child, truncated, sizeof(truncated), PARENT_SHORT);
	receive(child, other_dst, sizeof(other_dst), PARENT_SHORT);
	receive(child, routers, sizeof(routers), PARENT_SHORT);
	receive(child, own_src, sizeof(own_src), PARENT_SHORT);
	receive(child, command, sizeof(command), PARENT_SHORT);

	/* A secured frame with a modified MIC */
	zassert_ok(zzb_nlde_data_req(child, nsdu_buf(), PARENT_SHORT, 0, true));
	sent[0].data[sent[0].len - 1] ^= 0x01;
	receive(parent, sent[0].data, sent[0].len, CHILD_SHORT);

	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);
}

/*
 * R22 3.6.6: a multicast data frame goes up as a broadcast, since APS checks the group. The group
 * ID here equals the address of the node; a multicast command is dropped.
 */
ZTEST(zzb_nwk_data, test_multicast)
{
	struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_MULTICAST,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
	};
	const struct zzb_nwk_cmd status = {
		.id = ZZB_NWK_CMD_NWK_STATUS,
		.nwk_status = {.code = 0x0d, .has_addr = true, .addr = 0x5678},
	};

	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_ind_fake.arg2_val, 0xfffd);
	zassert_equal(zzb_nlde_data_ind_fake.arg3_val, PARENT_SHORT);
	zassert_equal(data_ind.len, sizeof(nsdu));

	hdr.type = ZZB_NWK_FRAME_CMD;
	receive_cmd(child, parent, &hdr, &status);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
}

/* R22 Table 3-69 and 3.6.6: 0xfffd and multicast frames go to devices with macRxOnWhenIdle TRUE */
ZTEST(zzb_nwk_data, test_sleepy_node_drops_frames_to_receivers_on_when_idle)
{
	struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY,
		.dst = 0xfffd,
		.src = PARENT_SHORT,
		.radius = 1,
	};

	child->sleepy = true;
	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	hdr.flags |= ZZB_NWK_MULTICAST;
	hdr.dst = 0x0001;
	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);

	hdr.flags = ZZB_NWK_SECURITY;
	hdr.dst = 0xffff;
	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_ind_fake.arg2_val, 0xffff);
	hdr.dst = CHILD_SHORT;
	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 2);
	zassert_equal(zzb_nlde_data_ind_fake.arg2_val, CHILD_SHORT);
}

/* R22 3.6.2.2 step 1 */
ZTEST(zzb_nwk_data, test_end_device_drops_frames_with_end_device_initiator)
{
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_DATA,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_ED_INITIATOR,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
	};

	receive_frame(child, parent, &hdr, nsdu, sizeof(nsdu));
	zassert_equal(zzb_nlde_data_ind_fake.call_count, 0);
}

ZTEST_SUITE(zzb_nwk_data, NULL, setup, before, after, NULL);
