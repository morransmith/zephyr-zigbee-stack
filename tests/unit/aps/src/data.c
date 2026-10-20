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

static int addr_map_find(struct zzb_node *n, uint64_t ext_addr, uint16_t *addr)
{
	*addr = 0x4444;
	return 0;
}

/* Section 11: a device that is neither the trust center nor a neighbor, through nwkAddressMap */
ZTEST(zzb_aps_data, test_address_map)
{
	const uint64_t remote = 0x00124b0000000099ULL;
	struct zzb_apsde_req req = on_off_req;

	req.dst_mode = ZZB_APS_ADDR_EXT;
	req.dst_ext = remote;
	zzb_nwk_addr_map_find_fake.custom_fake = addr_map_find;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x4444);

	/* The link key of a network address for the APS security */
	zzb_nwk_addr_map_ext_fake.return_val = remote;
	zassert_ok(zzb_apsme_set_link_key(node, remote, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	req.dst_mode = ZZB_APS_ADDR_SHORT;
	req.dst_addr = 0x4444;
	req.tx_options = ZZB_APS_TX_SECURITY;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);
}

ZTEST(zzb_aps_data, test_rejected_requests)
{
	struct zzb_apsde_req req = on_off_req;
	struct net_buf *buf = asdu_buf();

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
	zassert_false(ind.group);
	zassert_equal(data_ind.len, sizeof(asdu));
	zassert_mem_equal(data_ind.data, asdu, sizeof(asdu));
	zassert_equal(data_ind.meta.lqi, 200);
}

/* R22 2.2.8.4.2: one indication with the group address; ZCL serves the member endpoints */
ZTEST(zzb_aps_data, test_group_indication)
{
	/* Delivery mode group, group 0x1234, then the APS counter */
	uint8_t frame[] = {0x0c, 0x34, 0x12, 0x06, 0x00, 0x04, 0x01, 0x02, 0x57, 0x01, 0x42, 0x02};

	zassert_ok(zzb_apsme_add_group(node, 0x1234, 1));
	receive(frame, sizeof(frame), 0xfffd, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_true(ind.group);
	zassert_equal(ind.dst_addr, 0x1234);
	zassert_equal(ind.src_ep, 0x02);
	zassert_equal(ind.cluster, 0x0006);
	zassert_mem_equal(data_ind.data, asdu, sizeof(asdu));

	/* A duplicate */
	receive(frame, sizeof(frame), 0xfffd, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);

	/* A group without a member endpoint */
	frame[1] = 0x35;
	frame[8] = 0x58;
	receive(frame, sizeof(frame), 0xfffd, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);

	/* A group shares no link key */
	frame[0] |= ZZB_APS_SECURITY;
	frame[1] = 0x34;
	frame[8] = 0x59;
	receive(frame, sizeof(frame), 0xfffd, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
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

/* R22 2.2.8.4.2: a frame received again with the same source and APS counter is a duplicate */
ZTEST(zzb_aps_data, test_duplicate_is_delivered_once)
{
	static const uint8_t frame[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x60, 0x01};
	struct net_buf *buf;

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);

	/* The same counter from another source */
	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_add_mem(buf, frame, sizeof(frame));
	zzb_nlde_data_ind(node, buf, CHILD_ADDR, 0x2222, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 2);
}

/* An entry lives apscAckWaitDuration * (apscMaxFrameRetries + 1) */
ZTEST(zzb_aps_data, test_duplicate_entry_expires)
{
	static const uint8_t frame[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x61, 0x01};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	k_sleep(K_MSEC(6300));
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);

	k_sleep(K_MSEC(200));
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 2);
}

/* A full table replaces the entry that expires first */
ZTEST(zzb_aps_data, test_full_duplicate_table)
{
	uint8_t frame[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x70, 0x01};

	for (int i = 0; i <= CONFIG_ZZB_APS_DUP_COUNT; i++) {
		frame[7] = 0x70 + i;
		receive(frame, sizeof(frame), CHILD_ADDR, true);
		k_sleep(K_MSEC(10));
	}

	frame[7] = 0x71;
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	frame[7] = 0x70;
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, CONFIG_ZZB_APS_DUP_COUNT + 2);
}

static void check_data_ack(int i, uint8_t counter, uint8_t flags)
{
	struct zzb_aps_hdr hdr;

	zassert_equal(sent[i].meta.handle, 0xef);
	zassert_true(zzb_aps_hdr_parse(sent[i].data, sent[i].len, &hdr) > 0);
	zassert_equal(hdr.type, ZZB_APS_FRAME_ACK);
	zassert_equal(hdr.delivery, ZZB_APS_DELIVERY_UNICAST);
	zassert_equal(hdr.flags, flags);
	zassert_equal(hdr.dst_ep, 0x02);
	zassert_equal(hdr.src_ep, 0x01);
	zassert_equal(hdr.cluster, 0x0006);
	zassert_equal(hdr.profile, 0x0104);
	zassert_equal(hdr.counter, counter);
}

/* R22 2.2.5.2.3: the endpoints are swapped, cluster, profile and counter copied */
ZTEST(zzb_aps_data, test_ack)
{
	static const uint8_t frame[] = {0x40, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x62, 0x01};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x0000);
	zassert_true(zzb_nlde_data_req_fake.arg4_val);
	check_data_ack(0, 0x62, 0);
	zassert_equal(sent[0].len, 8);
}

/* The first ACK was lost, so the sender repeats the frame */
ZTEST(zzb_aps_data, test_duplicate_is_acknowledged_again)
{
	static const uint8_t frame[] = {0x40, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x63, 0x01};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);
	check_data_ack(1, 0x63, 0);
}

/* R22 2.2.8.4.3: broadcast frames are not acknowledged */
ZTEST(zzb_aps_data, test_no_ack_for_a_broadcast)
{
	static const uint8_t frame[] = {0x48, 0xff, 0x06, 0x00, 0x04, 0x01, 0x02, 0x64, 0x01};

	receive(frame, sizeof(frame), 0xfffd, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);
}

/* The ACK of a frame with APS security is secured with the same key */
ZTEST(zzb_aps_data, test_ack_with_link_key)
{
	static const uint8_t hdr[] = {0x60, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x65};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct zzb_aps_sec sec;

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(zzb_apsme_set_link_key(tc, NODE_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	net_buf_reserve(buf, ZZB_APS_AUX_MAX_LEN);
	net_buf_add_mem(buf, hdr, sizeof(hdr));
	net_buf_add_mem(buf, asdu, sizeof(asdu));
	zassert_ok(zzb_aps_secure(tc, buf, sizeof(hdr), key_b, ZZB_SEC_KEY_DATA, true));
	zzb_nlde_data_ind(node, buf, CHILD_ADDR, 0x0000, true);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);

	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_add_mem(buf, sent[0].data, sent[0].len);
	zassert_ok(zzb_aps_unsecure(tc, buf, 8, CHILD_ADDR, &sec));
	zassert_equal(sec.src_addr, NODE_EUI64);
	zassert_equal(buf->len, 8);
	net_buf_unref(buf);
	check_data_ack(0, 0x65, ZZB_APS_SECURITY);
}

ZTEST(zzb_aps_data, test_ack_error_drops_the_ack)
{
	static const uint8_t frame[] = {0x40, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x66, 0x01};

	zzb_nlde_data_req_fake.custom_fake = NULL;
	zzb_nlde_data_req_fake.return_val = -ENOENT;
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_apsde_data_ind_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
}

static const struct zzb_apsde_req acked_req = {
	.dst_mode = ZZB_APS_ADDR_SHORT,
	.dst_addr = 0x0000,
	.dst_ep = 0x02,
	.profile = 0x0104,
	.cluster = 0x0006,
	.src_ep = 0x01,
	.tx_options = ZZB_APS_TX_ACK,
};

/* The ACK of the trust center for counter */
static void receive_ack(uint8_t counter)
{
	const uint8_t frame[] = {0x02, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, counter};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
}

static uint8_t sent_counter(int i)
{
	struct zzb_aps_hdr hdr;

	zassert_true(zzb_aps_hdr_parse(sent[i].data, sent[i].len, &hdr) > 0);
	return hdr.counter;
}

/* Whether the last call of the zzb_nwk_poll_hold() fake set the hold of APS */
static bool held(void)
{
	return zzb_nwk_poll_hold_fake.arg1_val == ZZB_NWK_HOLD_APS &&
	       zzb_nwk_poll_hold_fake.arg2_val;
}

ZTEST(zzb_aps_data, test_acknowledged_transmission)
{
	struct zzb_aps_hdr hdr;

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	check_sent_hdr(0, &hdr);
	zassert_equal(hdr.flags, ZZB_APS_ACK_REQ);

	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 0);

	receive_ack(hdr.counter);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg1_val, HANDLE);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_SUCCESS);

	/* The ACK wait ends without a retransmission */
	k_sleep(K_MSEC(2000));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
}

/* The ACK is accepted before NLDE-DATA.confirm; the entry waits for that confirm */
ZTEST(zzb_aps_data, test_ack_before_the_confirm)
{
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	receive_ack(sent_counter(0));
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_SUCCESS);

	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	zassert_equal(sent[1].meta.handle, sent[0].meta.handle);
}

/* A sleepy node fetches the ACK from its parent at the short poll interval (section 5) */
ZTEST(zzb_aps_data, test_ack_wait_holds_the_poll_rate)
{
	struct net_buf *frame;

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &on_off_req));
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_false(held(), "unacknowledged frame");

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	zassert_false(held());
	zzb_nlde_data_conf(node, sent[1].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_true(held());
	receive_ack(sent_counter(1));
	zassert_false(held());

	/* The init of APS clears the table without a confirm */
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	zzb_nlde_data_conf(node, sent[2].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_true(held());
	frame = node->aps.tx[sent[2].meta.handle].frame;
	zassert_ok(reboot(node));
	net_buf_unref(frame);
	zassert_false(held());
}

/* The sender of a request, such as a coordinator that interviews the node, sends the next one */
ZTEST(zzb_aps_data, test_delivered_frame_polls_fast)
{
	static const uint8_t frame[] = {0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x67, 0x01};

	receive(frame, sizeof(frame), CHILD_ADDR, true);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 1);
	zassert_equal_ptr(zzb_nwk_poll_fast_fake.arg0_val, node);

	/* A duplicate and an ACK */
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	receive_ack(0x00);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 1);
}

/* R22 2.2.8.4.4: the same APS counter, a higher frame counter */
ZTEST(zzb_aps_data, test_retransmission_with_a_new_frame_counter)
{
	struct zzb_apsde_req req = acked_req;
	struct zzb_sec_aux_hdr aux[2];

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	req.tx_options |= ZZB_APS_TX_SECURITY;
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &req));
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);

	k_sleep(K_MSEC(1550));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);
	zassert_equal(sent[1].meta.handle, sent[0].meta.handle);
	zassert_equal(sent_counter(1), sent_counter(0));

	ARRAY_FOR_EACH(aux, i) {
		zassert_equal(zzb_sec_aux_hdr_parse(sent[i].data + 8, sent[i].len - 8, &aux[i]), 5);
	}
	zassert_true(aux[1].frame_counter > aux[0].frame_counter);

	zzb_nlde_data_conf(node, sent[1].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	receive_ack(sent_counter(1));
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_SUCCESS);
}

ZTEST(zzb_aps_data, test_no_ack_after_the_retries)
{
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	for (int i = 0; i <= 3; i++) {
		zassert_equal(zzb_nlde_data_req_fake.call_count, i + 1);
		zzb_nlde_data_conf(node, sent[i].meta.handle, ZZB_MAC_STATUS_SUCCESS);
		zassert_true(held());
		k_sleep(K_MSEC(1650));
	}

	zassert_equal(zzb_nlde_data_req_fake.call_count, 4);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg1_val, HANDLE);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_NO_ACK);
	zassert_false(held());
}

/* R22 2.2.4.1.1: the status of a failed NLDE-DATA.confirm comes before NO_ACK */
ZTEST(zzb_aps_data, test_failed_confirm_retries_at_once)
{
	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);

	for (int i = 1; i <= 3; i++) {
		zzb_nlde_data_conf(node, sent[i].meta.handle, ZZB_MAC_STATUS_SUCCESS);
		k_sleep(K_MSEC(1650));
	}

	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_MAC_STATUS_NO_ACK);
}

static int nlde_data_req_busy(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
			      uint8_t radius, bool secure)
{
	return -EBUSY;
}

/* A copy that cannot be sent counts as an attempt; the next one follows the ACK wait */
ZTEST(zzb_aps_data, test_send_error_retries_after_the_wait)
{
	int (*take)(struct zzb_node *, struct net_buf *, uint16_t, uint8_t, bool) =
		zzb_nlde_data_req_fake.custom_fake;

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	zzb_nlde_data_req_fake.custom_fake = nlde_data_req_busy;
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);

	zzb_nlde_data_req_fake.custom_fake = take;
	k_sleep(K_MSEC(1550));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);
	k_sleep(K_MSEC(100));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 3);

	/* The fake copies the frame of call n to sent[n - 1] */
	zzb_nlde_data_conf(node, sent[2].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	receive_ack(sent_counter(2));
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_SUCCESS);
}

/* R22 2.2.8.4.4: counter, cluster, source endpoint and source must match */
ZTEST(zzb_aps_data, test_unmatched_acks)
{
	uint8_t counter;
	uint8_t frame[] = {0x02, 0x01, 0x06, 0x00, 0x04, 0x01, 0x02, 0x00};
	struct net_buf *buf;

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &acked_req));
	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	counter = sent_counter(0);

	receive_ack(counter + 1);
	frame[7] = counter;
	frame[2] = 0x08;
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	frame[2] = 0x06;
	frame[6] = 0x03;
	receive(frame, sizeof(frame), CHILD_ADDR, true);
	frame[6] = 0x02;
	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_add_mem(buf, frame, sizeof(frame));
	zzb_nlde_data_ind(node, buf, CHILD_ADDR, 0x2222, true);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 0);

	receive_ack(counter);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
}

static const struct zzb_apsde_req bound_req = {
	.dst_mode = ZZB_APS_ADDR_BINDING,
	.profile = 0x0104,
	.cluster = 0x0006,
	.src_ep = 0x01,
	.tx_options = ZZB_APS_TX_ACK,
};

static void bind(enum zzb_aps_addr_mode mode, uint16_t group, uint64_t ext)
{
	const struct zzb_aps_binding binding = {
		.src_ep = 0x01,
		.cluster = 0x0006,
		.dst_mode = mode,
		.dst_addr = group,
		.dst_ext = ext,
		.dst_ep = 0x02,
	};

	zassert_ok(zzb_apsme_bind_req(node, &binding));
}

static void check_tx_free(void)
{
	ARRAY_FOR_EACH_PTR(node->aps.tx, tx) {
		zassert_false(tx->in_use);
	}
}

/* R22 2.2.4.1.1: a frame per bound destination, one confirm */
ZTEST(zzb_aps_data, test_bound_destinations)
{
	struct zzb_aps_hdr hdr[2];

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	bind(ZZB_APS_ADDR_GROUP, 0x0002, 0);
	/* Neither the trust center nor a neighbor */
	bind(ZZB_APS_ADDR_EXT, 0, 0x00124b0000000099ULL);
	bind(ZZB_APS_ADDR_EXT, 0, TC_EUI64);

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &bound_req));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 2);

	check_sent_hdr(0, &hdr[0]);
	zassert_equal(zzb_nlde_data_req_fake.arg2_history[0], 0xfffd);
	zassert_equal(hdr[0].delivery, ZZB_APS_DELIVERY_GROUP);
	zassert_equal(hdr[0].group, 0x0002);
	zassert_equal(hdr[0].flags, 0);

	check_sent_hdr(1, &hdr[1]);
	zassert_equal(zzb_nlde_data_req_fake.arg2_history[1], 0x0000);
	zassert_equal(hdr[1].delivery, ZZB_APS_DELIVERY_UNICAST);
	zassert_equal(hdr[1].dst_ep, 0x02);
	zassert_equal(hdr[1].flags, ZZB_APS_ACK_REQ);
	/* A device bound twice would drop a second frame with the same counter (R22 2.2.8.4.2) */
	zassert_not_equal(hdr[0].counter, hdr[1].counter);

	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zzb_nlde_data_conf(node, sent[1].meta.handle, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 0);

	receive_ack(hdr[1].counter);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg1_val, HANDLE);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_APS_STATUS_SUCCESS);
	check_tx_free();
}

/* The confirm carries the status of the first failed destination in binding order */
ZTEST(zzb_aps_data, test_bound_confirm_status)
{
	bind(ZZB_APS_ADDR_GROUP, 0x0002, 0);
	bind(ZZB_APS_ADDR_GROUP, 0x0003, 0);

	zassert_ok(zzb_apsde_data_req(node, asdu_buf(), &bound_req));
	zzb_nlde_data_conf(node, sent[1].meta.handle, ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 0);

	zzb_nlde_data_conf(node, sent[0].meta.handle, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
	zassert_equal(zzb_apsde_data_conf_fake.call_count, 1);
	zassert_equal(zzb_apsde_data_conf_fake.arg2_val, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
	check_tx_free();
}

ZTEST(zzb_aps_data, test_bound_rejected_requests)
{
	struct zzb_apsde_req req = bound_req;
	struct net_buf *buf = asdu_buf();

	/* R22 NO_BOUND_DEVICE */
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOENT);

	for (int i = 0; i <= CONFIG_ZZB_APS_TX_COUNT; i++) {
		bind(ZZB_APS_ADDR_GROUP, i, 0);
	}
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOBUFS);

	req.tx_options |= ZZB_APS_TX_SECURITY;
	zassert_equal(zzb_apsde_data_req(node, buf, &req), -ENOTSUP);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);

	/* An error of the first destination is returned, and nothing is sent */
	zzb_aps_reset(node);
	bind(ZZB_APS_ADDR_GROUP, 0x0002, 0);
	bind(ZZB_APS_ADDR_GROUP, 0x0003, 0);
	zzb_nlde_data_req_fake.custom_fake = NULL;
	zzb_nlde_data_req_fake.return_val = -ENOTCONN;
	zassert_equal(zzb_apsde_data_req(node, buf, &bound_req), -ENOTCONN);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	net_buf_unref(buf);
	check_tx_free();
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
