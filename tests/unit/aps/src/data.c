/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "sec/ccm.h"

#define HANDLE     7
#define CHILD_ADDR 0x1234

/* On/Off command Toggle */
static const uint8_t asdu[] = {0x01, 0x42, 0x02};

static struct zzb_node *node;
static struct zzb_node *tc;
static struct zzb_nwk_neighbor parent;

static const struct zzb_apsde_req on_off_req = {
	.dst_mode = ZZB_APS_ADDR_SHORT,
	.dst_addr = 0x5678,
	.dst_ep = 0x02,
	.profile = 0x0104,
	.cluster = 0x0006,
	.src_ep = 0x01,
};

static struct net_buf *asdu_buf(void)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_APSDE_HEADROOM);
	net_buf_add_mem(buf, asdu, sizeof(asdu));
	zzb_frame_meta(buf)->handle = HANDLE;
	return buf;
}

static void check_sent_hdr(int i, struct zzb_aps_hdr *hdr)
{
	int hdr_len = zzb_aps_hdr_parse(sent[i].data, sent[i].len, hdr);

	zassert_true(hdr_len > 0);
	zassert_equal(hdr->type, ZZB_APS_FRAME_DATA);
	zassert_equal(hdr->profile, 0x0104);
	zassert_equal(hdr->cluster, 0x0006);
	zassert_equal(hdr->src_ep, 0x01);
	if (!(hdr->flags & ZZB_APS_SECURITY)) {
		zassert_equal(sent[i].len, hdr_len + sizeof(asdu));
		zassert_mem_equal(sent[i].data + hdr_len, asdu, sizeof(asdu));
	}
}

/* Passes a frame from the trust center to the node as NLDE-DATA.indication */
static void receive(const uint8_t *frame, size_t len, uint16_t dst_addr, bool security_use)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, frame, len);
	zzb_frame_meta(buf)->lqi = 200;
	zzb_nlde_data_ind(node, buf, dst_addr, 0x0000, security_use);
}

static void *setup(void)
{
	node = test_node(0);
	tc = test_node(1);
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	zzb_aps_reset(node);
	zzb_aps_reset(tc);
	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		tx->in_use = false;
	}

	parent = (struct zzb_nwk_neighbor){
		.in_use = true,
		.ext_addr = 0x00124b0000000001ULL,
		.short_addr = 0x0000,
		.relationship = ZZB_NWK_REL_PARENT,
	};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

ZTEST(zzb_aps_data, test_unicast)
{
	uint8_t counter = node->aps.counter;
	struct zzb_aps_hdr hdr;

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x5678);
	zassert_equal(zzb_nlde_data_req_fake.arg3_val, 0);
	zassert_true(zzb_nlde_data_req_fake.arg4_val);
	zassert_equal(sent[0].meta.handle, 0);

	check_sent_hdr(0, &hdr);
	zassert_equal(hdr.delivery, ZZB_APS_DELIVERY_UNICAST);
	zassert_equal(hdr.flags, 0);
	zassert_equal(hdr.dst_ep, 0x02);
	zassert_equal(hdr.counter, counter);
	zassert_equal(node->aps.counter, (uint8_t)(counter + 1));
}

/* The NSDU handle is the entry index; the confirm carries the ASDU handle */
ZTEST(zzb_aps_data, test_confirm)
{
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal_ptr(zzb_apsde_data_conf_fake.arg0_val, node);
	zassert_equal(zzb_apsde_data_conf_fake.arg1_val, HANDLE);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_MAC_STATUS_NO_ACK);

	/* The entry is free */
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zzb_nlde_data_conf(node, 0xef, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
}

ZTEST(zzb_aps_data, test_broadcast_and_group)
{
	struct zzb_apsde_req req = on_off_req;
	struct zzb_aps_hdr hdr;

	req.dst_addr = 0xfffd;
	req.dst_ep = 0xff;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	check_sent_hdr(0, &hdr);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0xfffd);
	zassert_equal(hdr.delivery, ZZB_APS_DELIVERY_BROADCAST);
	zassert_equal(hdr.dst_ep, 0xff);

	req.dst_mode = ZZB_APS_ADDR_GROUP;
	req.dst_addr = 0x0002;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	check_sent_hdr(1, &hdr);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0xfffd);
	zassert_equal(hdr.delivery, ZZB_APS_DELIVERY_GROUP);
	zassert_equal(hdr.group, 0x0002);
}

/* The trust center is at 0x0000 (R22 4.6.2.2); neighbors are known by the neighbor table */
ZTEST(zzb_aps_data, test_ieee_destination)
{
	struct zzb_apsde_req req = on_off_req;
	struct net_buf *buf = asdu_buf();

	req.dst_mode = ZZB_APS_ADDR_EXT;
	req.dst_ext = TC_EUI64;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOENT);
	net_buf_unref(buf);

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x0000);

	parent.short_addr = 0x2222;
	zzb_nwk_neighbor_find_ext_fake.return_val = &parent;
	req.dst_ext = parent.ext_addr;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x2222);

	/* An entry from a beacon has no relationship */
	parent.relationship = ZZB_NWK_REL_NONE;
	buf = asdu_buf();
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOENT);
	net_buf_unref(buf);
}

ZTEST(zzb_aps_data, test_rejected_requests)
{
	struct zzb_apsde_req req = on_off_req;
	struct net_buf *buf = asdu_buf();

	req.dst_mode = ZZB_APS_ADDR_BINDING;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOTSUP);

	req = on_off_req;
	req.tx_options = ZZB_APS_TX_FRAGMENT;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOTSUP);

	/* R22 2.2.8.4.3: broadcast and group frames are not acknowledged */
	req = on_off_req;
	req.tx_options = ZZB_APS_TX_ACK;
	req.dst_addr = 0xffff;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -EINVAL);
	net_buf_unref(buf);

	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM + 8 - 1);
	zassert_equal(zzb_apsde_data_req(node, buf, &on_off_req), -EMSGSIZE);
	net_buf_unref(buf);

	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);
}

/* An error of NLDE-DATA.request leaves the buffer with the caller and frees the entry */
ZTEST(zzb_aps_data, test_errors_are_returned)
{
	struct net_buf *buf = asdu_buf();

	zzb_nlde_data_req_fake.custom_fake = NULL;
	zzb_nlde_data_req_fake.return_val = -ENOTCONN;
	for (int i = 0; i <= CONFIG_ZZB_APS_TX_COUNT; i++) {
		net_buf_reset(buf);
		net_buf_reserve(buf, ZZB_APSDE_HEADROOM);
		net_buf_add_mem(buf, asdu, sizeof(asdu));
		zassert_equal(zzb_apsde_data_req(node, buf, &on_off_req), -ENOTCONN);
	}

	net_buf_unref(buf);
}

ZTEST(zzb_aps_data, test_full_table)
{
	struct net_buf *buf;

	for (int i = 0; i < CONFIG_ZZB_APS_TX_COUNT; i++) {
		zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	}

	buf = asdu_buf();
	zassert_equal(zzb_apsde_data_req(node, buf, &on_off_req), -ENOBUFS);
	net_buf_unref(buf);

	zzb_nlde_data_conf(node, sent[1].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	zassert_equal(sent[CONFIG_ZZB_APS_TX_COUNT].meta.handle, sent[1].meta.handle);
}

/* R22 4.4.1.1: data frames are secured with the data key of the destination */
ZTEST(zzb_aps_data, test_link_key_security)
{
	struct zzb_apsde_req req = on_off_req;
	struct net_buf *buf = asdu_buf();
	struct zzb_aps_hdr hdr;
	struct zzb_aps_sec sec;
	int hdr_len;

	req.dst_addr = 0x0000;
	req.tx_options = ZZB_APS_TX_SECURITY | ZZB_APS_TX_EXT_NONCE;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOENT);

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOENT, "unverified key");
	net_buf_unref(buf);

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	check_sent_hdr(0, &hdr);
	zassert_equal(hdr.flags, ZZB_APS_SECURITY);

	/* The trust center removes the security */
	zassert_ok(zzb_apsme_set_link_key(tc, NODE_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_add_mem(buf, sent[0].data, sent[0].len);
	hdr_len = zzb_aps_hdr_len(&hdr);
	zassert_ok(zzb_aps_unsecure(tc, buf, hdr_len, CHILD_ADDR, &sec));
	zassert_equal(sec.src_addr, NODE_EUI64);
	zassert_mem_equal(buf->data + hdr_len, asdu, sizeof(asdu));
	net_buf_unref(buf);
}

ZTEST(zzb_aps_data, test_indication)
{
	static const uint8_t frame[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01,
					0x02, 0x55, 0x01, 0x42, 0x02};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_apsde_data_ind_fake.arg0_val, node);
	zassert_equal(ind.dst_addr, CHILD_ADDR);
	zassert_equal(ind.dst_ep, 0x01);
	zassert_equal(ind.src_addr, 0x0000);
	zassert_equal(ind.src_ep, 0x02);
	zassert_equal(ind.profile, 0x0104);
	zassert_equal(ind.cluster, 0x0006);
	zassert_equal(ind.security, ZZB_APS_SECURED_NWK_KEY);
	zassert_equal(data_ind.len, sizeof(asdu));
	zassert_mem_equal(data_ind.data, asdu, sizeof(asdu));
	zassert_equal(data_ind.meta.lqi, 200);
}

ZTEST(zzb_aps_data, test_link_key_secured_indication)
{
	static const uint8_t hdr[] = {0x20, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x56};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	net_buf_reserve(buf, ZZB_APS_AUX_MAX_LEN);
	net_buf_add_mem(buf, hdr, sizeof(hdr));
	net_buf_add_mem(buf, asdu, sizeof(asdu));
	zassert_ok(zzb_aps_secure(tc, buf, sizeof(hdr), key_b, ZZB_SEC_KEY_DATA, true));
	zzb_frame_meta(buf)->lqi = 200;
	zzb_nlde_data_ind(node, buf, CHILD_ADDR, 0x0000, true);

	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal(ind.security, ZZB_APS_SECURED_LINK_KEY);
	zassert_equal(data_ind.len, sizeof(asdu));
	zassert_mem_equal(data_ind.data, asdu, sizeof(asdu));
}

ZTEST(zzb_aps_data, test_dropped_frames)
{
	static const uint8_t data[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x57, 0x01};
	/* Without a group table no endpoint is a member */
	static const uint8_t group[] = {0x0c, 0x02, 0x00, 0x06, 0x00, 0x04, 0x01, 0x02, 0x58, 0x01};
	/* The security flag without a key */
	static const uint8_t secured[] = {0x20, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x59, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};

	/* R22 3.6.2.2: unsecured data frames reach APS only for the Transport Key */
	receive(data, sizeof(data), CHILD_ADDR, false);
	receive(group, sizeof(group), 0xfffd, true);
	receive(secured, sizeof(secured), CHILD_ADDR, true);
	receive(data, 3, CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 0);
}

ZTEST_SUITE(zzb_aps_data, NULL, setup, before, after, NULL);
