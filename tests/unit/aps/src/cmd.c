/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "sec/hmac.h"

#define CHILD_ADDR  0x1234
#define ROUTER_ADDR 0x2222

static const uint8_t nwk_key[ZZB_SEC_KEY_LEN] = {0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
						 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf};

static struct zzb_node *node;
static struct zzb_node *tc;
static uint8_t tc_counter;

/* How the trust center sends a command */
struct tc_frame {
	const uint8_t *link_key;
	enum zzb_sec_key_id key_id;
	uint8_t flags;
	uint16_t nwk_src;
	uint16_t nwk_dst;
	bool nwk_security;
	bool no_ext_nonce;
};

/* The network key Transport Key of a trust center that the parent relays */
static const struct tc_frame transport = {
	.link_key = zzb_aps_default_tc_link_key,
	.key_id = ZZB_SEC_KEY_TRANSPORT,
	.flags = ZZB_APS_SECURITY,
	.nwk_src = ROUTER_ADDR,
	.nwk_dst = CHILD_ADDR,
};

static void receive_cmd_at(struct zzb_node *to, const struct zzb_aps_cmd *cmd,
			   const struct tc_frame *how)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_CMD,
		.flags = how->flags,
		.counter = tc_counter++,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	int err;

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_APS_AUX_MAX_LEN);
	zzb_aps_hdr_write(&hdr, net_buf_add(buf, 2));
	net_buf_add(buf, zzb_aps_cmd_write(cmd, net_buf_tail(buf)));
	if (how->flags & ZZB_APS_SECURITY) {
		err = zzb_aps_secure(tc, buf, 2, how->link_key, how->key_id, !how->no_ext_nonce);
		zassert_ok(err);
	}

	zzb_nlde_data_ind(to, buf, how->nwk_dst, how->nwk_src, how->nwk_security);
}

static void receive_cmd(const struct zzb_aps_cmd *cmd, const struct tc_frame *how)
{
	receive_cmd_at(node, cmd, how);
}

static struct zzb_aps_cmd nwk_key_cmd(uint64_t dst, uint64_t src)
{
	struct zzb_aps_cmd cmd = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_NWK,
		.transport_key.seq = 1,
		.transport_key.dst = dst,
		.transport_key.src = src,
	};

	memcpy(cmd.transport_key.key, nwk_key, ZZB_SEC_KEY_LEN);
	return cmd;
}

static void check_transport_key_ind(int count, uint64_t src)
{
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, count);
	zassert_equal_ptr(zzb_apsme_transport_key_ind_fake.arg0_val, node);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg1_val, src);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg2_val, ZZB_APS_KEY_NWK);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg4_val, 1);
	zassert_mem_equal(transported_key, nwk_key, ZZB_SEC_KEY_LEN);
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
	zzb_nwk_key_pending_fake.return_val = true;
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* The zigbee-rs frame of tests/unit/sec (see NOTICE), without NWK security */
ZTEST(zzb_aps_cmd, test_captured_transport_key)
{
	static const uint8_t frame[] = {
		0x21, 0x95, 0x30, 0x00, 0x00, 0x00, 0x00, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce,
		0xf4, 0xcc, 0x56, 0x50, 0x5e, 0x07, 0x2d, 0xc5, 0xc1, 0xe8, 0x40, 0xf2, 0xd5, 0xce,
		0x0c, 0xa9, 0x2d, 0x64, 0x23, 0xcc, 0x0c, 0x56, 0xcc, 0xc4, 0xcc, 0x0f, 0x18, 0xa2,
		0xe4, 0x82, 0x88, 0x58, 0x4a, 0x90, 0x3e, 0x00, 0x47, 0x60, 0xf2, 0x5d,
	};
	static const uint8_t key[] = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	net_buf_add_mem(buf, frame, sizeof(frame));
	zzb_nlde_data_ind(node, buf, CHILD_ADDR, 0x0000, false);

	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg1_val, TC_EUI64);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg2_val, ZZB_APS_KEY_NWK);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg4_val, 0);
	zassert_mem_equal(transported_key, key, sizeof(key));
	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);
}

static int ind_nlde_calls;

static void transport_key_ind_count(struct zzb_node *n, uint64_t src_addr,
				    enum zzb_aps_key_type type, const uint8_t *key, uint8_t seq)
{
	ind_nlde_calls = zzb_nlde_data_req_fake.call_count;
}

/*
 * R23 4.4.11: a trust center that is the parent requests an ACK. It follows the indication, in
 * which ZDO sets the network key that secures it.
 */
ZTEST(zzb_aps_cmd, test_transport_key_with_ack_request)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);
	struct tc_frame how = transport;
	struct zzb_aps_hdr hdr;

	how.flags |= ZZB_APS_ACK_REQ;
	how.nwk_src = 0x0000;
	zzb_apsme_transport_key_ind_fake.custom_fake = transport_key_ind_count;
	receive_cmd(&cmd, &how);

	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1);
	zassert_equal(ind_nlde_calls, 0);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x0000);
	zassert_true(zzb_nlde_data_req_fake.arg4_val);
	zassert_true(zzb_aps_hdr_parse(sent[0].data, sent[0].len, &hdr) > 0);
	zassert_equal(hdr.type, ZZB_APS_FRAME_ACK);
	zassert_equal(hdr.flags, ZZB_APS_ACK_FORMAT | ZZB_APS_SECURITY);
}

/*
 * BDB 6.4: a node with an install code takes the network key under the install code key or under
 * the default key. The trust center entry gets the key that decrypted it (R22 4.6.3.1).
 */
ZTEST(zzb_aps_cmd, test_transport_key_with_an_install_code)
{
	struct zzb_node *ic_node = test_node(2);
	struct zzb_aps_cmd cmd = nwk_key_cmd(IC_NODE_EUI64, TC_EUI64);
	struct tc_frame how = transport;
	const struct zzb_aps_key_pair *pair;

	zzb_aps_reset(ic_node);
	how.link_key = install_code_key;
	receive_cmd_at(ic_node, &cmd, &how);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1);
	zassert_ok(zzb_apsme_set_tc_addr(ic_node, TC_EUI64));
	pair = zzb_aps_key_pair_find(ic_node, TC_EUI64);
	zassert_mem_equal(pair->key, install_code_key, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_UNIQUE);
	zassert_equal(pair->attr, ZZB_APS_KEY_PROVISIONAL);

	/* A network that does not know the install code */
	zzb_aps_reset(ic_node);
	how.link_key = zzb_aps_default_tc_link_key;
	receive_cmd_at(ic_node, &cmd, &how);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 2);
	zassert_ok(zzb_apsme_set_tc_addr(ic_node, TC_EUI64));
	pair = zzb_aps_key_pair_find(ic_node, TC_EUI64);
	zassert_mem_equal(pair->key, zzb_aps_default_tc_link_key, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_GLOBAL);

	/* Neither key */
	zzb_aps_reset(ic_node);
	how.link_key = key_b;
	receive_cmd_at(ic_node, &cmd, &how);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 2);
}

/* The ACK of a frame decrypted with a preconfigured key is secured with the same key */
ZTEST(zzb_aps_cmd, test_ack_under_the_install_code_key)
{
	struct zzb_node *ic_node = test_node(2);
	struct zzb_aps_cmd cmd = nwk_key_cmd(IC_NODE_EUI64, TC_EUI64);
	struct tc_frame how = transport;
	struct zzb_aps_sec sec;
	struct net_buf *buf;

	zzb_aps_reset(ic_node);
	how.link_key = install_code_key;
	how.flags |= ZZB_APS_ACK_REQ;
	how.nwk_src = 0x0000;
	receive_cmd_at(ic_node, &cmd, &how);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);

	zassert_ok(zzb_apsme_set_link_key(tc, IC_NODE_EUI64, install_code_key, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	buf = zzb_buf_alloc(K_NO_WAIT);
	net_buf_add_mem(buf, sent[0].data, sent[0].len);
	zassert_ok(zzb_aps_unsecure(tc, buf, 2, CHILD_ADDR, &sec));
	zassert_equal(sec.src_addr, IC_NODE_EUI64);
	net_buf_unref(buf);
	zzb_aps_reset(tc);
}

/* Under a router parent the NWK source is the parent (R22 4.6.3.2.4) */
ZTEST(zzb_aps_cmd, test_transport_key_through_a_router)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);

	receive_cmd(&cmd, &transport);
	check_transport_key_ind(1, TC_EUI64);

	/* A broadcast network key has an all-zero destination address field */
	cmd = nwk_key_cmd(0, TC_EUI64);
	receive_cmd(&cmd, &transport);
	check_transport_key_ind(2, TC_EUI64);
}

/* Z-Stack Home 1.2: the parent sends the key without the extended nonce */
ZTEST(zzb_aps_cmd, test_transport_key_without_extended_nonce)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);
	struct zzb_nwk_neighbor parent = {
		.in_use = true,
		.ext_addr = TC_EUI64,
		.relationship = ZZB_NWK_REL_PARENT,
	};
	struct tc_frame how = transport;

	how.nwk_src = 0x0000;
	how.no_ext_nonce = true;
	zzb_nwk_neighbor_find_short_fake.return_val = &parent;
	receive_cmd(&cmd, &how);
	check_transport_key_ind(1, TC_EUI64);
}

/* BDB 10.2.2: once the trust center is known, other sources are dropped */
ZTEST(zzb_aps_cmd, test_transport_key_from_another_trust_center)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);

	zassert_ok(zzb_apsme_set_tc_addr(node, 0x00124b0000000001ULL));
	receive_cmd(&cmd, &transport);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 0);
}

ZTEST(zzb_aps_cmd, test_rejected_transport_keys)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(0x0011223344556677ULL, TC_EUI64);
	struct tc_frame how = transport;

	/* For another device */
	receive_cmd(&cmd, &transport);

	/* Distributed security is excluded */
	cmd = nwk_key_cmd(NODE_EUI64, UINT64_MAX);
	receive_cmd(&cmd, &transport);

	/* The source address field must be the device that secured the command */
	cmd = nwk_key_cmd(NODE_EUI64, 0x00124b0000000001ULL);
	receive_cmd(&cmd, &transport);

	/* A network key comes under the key-transport key (R22 4.4.1.1 step 1) */
	cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);
	how.key_id = ZZB_SEC_KEY_DATA;
	receive_cmd(&cmd, &how);

	/* R22 4.4.1.5: a device with a preconfigured key needs APS security */
	how = transport;
	how.flags = 0;
	how.nwk_security = true;
	receive_cmd(&cmd, &how);

	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 0);
}

/* R22 4.4.1.5: a NWK broadcast from the trust center with NWK security only */
ZTEST(zzb_aps_cmd, test_network_key_update_without_aps_security)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(0, TC_EUI64);
	struct tc_frame how = {
		.nwk_src = 0x0000,
		.nwk_dst = 0xfffd,
		.nwk_security = true,
	};

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	receive_cmd(&cmd, &how);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 0, "network key pending");

	zzb_nwk_key_pending_fake.return_val = false;
	receive_cmd(&cmd, &how);
	check_transport_key_ind(1, TC_EUI64);

	how.nwk_dst = CHILD_ADDR;
	receive_cmd(&cmd, &how);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1, "unicast");
}

ZTEST(zzb_aps_cmd, test_repeated_transport_key)
{
	struct zzb_aps_cmd cmd = nwk_key_cmd(NODE_EUI64, TC_EUI64);

	receive_cmd(&cmd, &transport);
	tc_counter--;
	receive_cmd(&cmd, &transport);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1);
}

/* The trust center link key of BDB 10.2.5 step 8, under the key-load key of key A */
static const struct tc_frame key_load = {
	.link_key = zzb_aps_default_tc_link_key,
	.key_id = ZZB_SEC_KEY_LOAD,
	.flags = ZZB_APS_SECURITY,
	.nwk_src = 0x0000,
	.nwk_dst = CHILD_ADDR,
	.nwk_security = true,
};

static struct zzb_aps_cmd tc_link_key_cmd(uint64_t dst)
{
	struct zzb_aps_cmd cmd = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_TC_LINK,
		.transport_key.dst = dst,
		.transport_key.src = TC_EUI64,
	};

	memcpy(cmd.transport_key.key, key_b, ZZB_SEC_KEY_LEN);
	return cmd;
}

/* The node joined: the trust center is known and the network key is set */
static void authorize(void)
{
	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zzb_nwk_key_pending_fake.return_val = false;
}

ZTEST(zzb_aps_cmd, test_trust_center_link_key)
{
	struct zzb_aps_cmd cmd = tc_link_key_cmd(NODE_EUI64);

	authorize();
	receive_cmd(&cmd, &key_load);
	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 1);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg1_val, TC_EUI64);
	zassert_equal(zzb_apsme_transport_key_ind_fake.arg2_val, ZZB_APS_KEY_TC_LINK);
	zassert_mem_equal(transported_key, key_b, ZZB_SEC_KEY_LEN);
}

ZTEST(zzb_aps_cmd, test_rejected_trust_center_link_keys)
{
	struct zzb_aps_cmd cmd = tc_link_key_cmd(NODE_EUI64);
	struct tc_frame how = key_load;

	/* Only a joined and authorized node takes it (R22 4.4.2.3) */
	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	receive_cmd(&cmd, &key_load);

	/* A link key comes under the key-load key (R22 4.4.1.1 step 1) */
	zzb_nwk_key_pending_fake.return_val = false;
	how.key_id = ZZB_SEC_KEY_TRANSPORT;
	receive_cmd(&cmd, &how);

	/* The all-zero destination belongs to a network key */
	cmd = tc_link_key_cmd(0);
	receive_cmd(&cmd, &key_load);

	zassert_equal(zzb_apsme_transport_key_ind_fake.call_count, 0);
}

static const struct zzb_aps_cmd switch_key = {
	.id = ZZB_APS_CMD_SWITCH_KEY,
	.switch_key_seq = 1,
};

/* R22 4.4.1.3: without APS security the trust center is NWK address 0x0000 */
ZTEST(zzb_aps_cmd, test_switch_key)
{
	struct tc_frame how = {
		.nwk_src = 0x0000,
		.nwk_dst = 0xfffd,
		.nwk_security = true,
	};

	authorize();
	receive_cmd(&switch_key, &how);
	zassert_equal(zzb_apsme_switch_key_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_apsme_switch_key_ind_fake.arg0_val, node);
	zassert_equal(zzb_apsme_switch_key_ind_fake.arg1_val, TC_EUI64);
	zassert_equal(zzb_apsme_switch_key_ind_fake.arg2_val, 1);

	how.nwk_src = ROUTER_ADDR;
	receive_cmd(&switch_key, &how);
	zassert_equal(zzb_apsme_switch_key_ind_fake.call_count, 1);

	/* With APS security the key identifies the trust center */
	how.link_key = zzb_aps_default_tc_link_key;
	how.key_id = ZZB_SEC_KEY_DATA;
	how.flags = ZZB_APS_SECURITY;
	receive_cmd(&switch_key, &how);
	zassert_equal(zzb_apsme_switch_key_ind_fake.call_count, 2);
}

ZTEST(zzb_aps_cmd, test_rejected_switch_keys)
{
	struct tc_frame how = {
		.link_key = zzb_aps_default_tc_link_key,
		.key_id = ZZB_SEC_KEY_DATA,
		.flags = ZZB_APS_SECURITY,
		.nwk_src = 0x0000,
		.nwk_dst = 0xfffd,
	};

	/* Without NWK security only a Transport Key is accepted */
	authorize();
	receive_cmd(&switch_key, &how);

	/* While the network key is pending */
	how.nwk_security = true;
	zzb_nwk_key_pending_fake.return_val = true;
	receive_cmd(&switch_key, &how);

	zassert_equal(zzb_apsme_switch_key_ind_fake.call_count, 0);
}

/* Parses the command of sent frame i, removing its APS security at the trust center */
static void parse_sent_cmd(int i, struct zzb_aps_hdr *hdr, struct zzb_aps_cmd *cmd)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct zzb_aps_sec sec;
	int hdr_len;

	net_buf_add_mem(buf, sent[i].data, sent[i].len);
	hdr_len = zzb_aps_hdr_parse(buf->data, buf->len, hdr);
	zassert_equal(hdr_len, 2);
	if (hdr->flags & ZZB_APS_SECURITY) {
		zassert_ok(zzb_aps_unsecure(tc, buf, hdr_len, CHILD_ADDR, &sec));
		zassert_equal(sec.key_id, ZZB_SEC_KEY_DATA);
		zassert_equal(sec.src_addr, NODE_EUI64);
	}

	zassert_true(zzb_aps_cmd_parse(buf->data + hdr_len, buf->len - hdr_len, cmd) > 0);
	net_buf_unref(buf);
}

/* BDB 10.2.5 step 7: Request Key under key A; R22 4.4.10 sends commands without ACK request */
ZTEST(zzb_aps_cmd, test_request_key)
{
	uint8_t counter = node->aps.counter;
	struct zzb_aps_hdr hdr;
	struct zzb_aps_cmd cmd;

	authorize();
	zzb_aps_reset(tc);
	zassert_ok(zzb_apsme_set_link_key(tc, NODE_EUI64, zzb_aps_default_tc_link_key,
					  ZZB_APS_KEY_PROVISIONAL, ZZB_APS_LINK_KEY_GLOBAL));
	zassert_ok(zzb_apsme_request_key_req(node, TC_EUI64));

	zassert_equal(zzb_nlde_data_req_fake.call_count, 1);
	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x0000);
	zassert_true(zzb_nlde_data_req_fake.arg4_val);
	zassert_equal(sent[0].meta.handle, 0xef);
	parse_sent_cmd(0, &hdr, &cmd);
	zassert_equal(hdr.type, ZZB_APS_FRAME_CMD);
	zassert_equal(hdr.flags, ZZB_APS_SECURITY);
	zassert_equal(hdr.counter, counter);
	zassert_equal(node->aps.counter, (uint8_t)(counter + 1));
	zassert_equal(cmd.id, ZZB_APS_CMD_REQUEST_KEY);
	zassert_equal(cmd.request_key.type, ZZB_APS_REQUEST_TC_LINK);
}

ZTEST(zzb_aps_cmd, test_rejected_key_requests)
{
	zassert_equal(zzb_apsme_request_key_req(node, TC_EUI64), -EINVAL, "no trust center");

	authorize();
	zassert_equal(zzb_apsme_request_key_req(node, 0x00124b0000000001ULL), -EINVAL);

	/* R22 4.4.1.1 step 1: an unverified key does not encrypt */
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_equal(zzb_apsme_request_key_req(node, TC_EUI64), -ENOENT);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);
}

/* BDB 10.2.5 step 11: the hash of key B, without APS security (R22 4.4.7.1.3) */
ZTEST(zzb_aps_cmd, test_verify_key)
{
	uint8_t hash[ZZB_SEC_KEY_LEN];
	struct zzb_aps_hdr hdr;
	struct zzb_aps_cmd cmd;

	authorize();
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(zzb_apsme_verify_key_req(node, TC_EUI64, ZZB_APS_KEY_TC_LINK));

	zassert_equal(zzb_nlde_data_req_fake.arg2_val, 0x0000);
	parse_sent_cmd(0, &hdr, &cmd);
	zassert_equal(hdr.flags, 0);
	zassert_equal(cmd.id, ZZB_APS_CMD_VERIFY_KEY);
	zassert_equal(cmd.verify_key.type, ZZB_APS_KEY_TC_LINK);
	zassert_equal(cmd.verify_key.src, NODE_EUI64);
	zassert_ok(zzb_sec_hmac(key_b, 0x03, hash));
	zassert_mem_equal(cmd.verify_key.hash, hash, ZZB_SEC_KEY_LEN);
}

ZTEST(zzb_aps_cmd, test_rejected_verify_keys)
{
	zassert_equal(zzb_apsme_verify_key_req(node, TC_EUI64, ZZB_APS_KEY_TC_LINK), -EINVAL,
		      "no trust center");

	authorize();
	zassert_equal(zzb_apsme_verify_key_req(node, TC_EUI64, ZZB_APS_KEY_NWK), -EINVAL);
	zassert_equal(zzb_apsme_verify_key_req(node, 0x00124b0000000001ULL, ZZB_APS_KEY_TC_LINK),
		      -EINVAL);

	zzb_aps_reset(node);
	node->aps.aib.tc_addr = TC_EUI64;
	zassert_equal(zzb_apsme_verify_key_req(node, TC_EUI64, ZZB_APS_KEY_TC_LINK), -ENOENT);
	zassert_equal(zzb_nlde_data_req_fake.call_count, 0);
}

/* The Confirm Key of BDB 10.2.5 step 12, under key B */
static const struct tc_frame confirm = {
	.link_key = key_b,
	.key_id = ZZB_SEC_KEY_DATA,
	.flags = ZZB_APS_SECURITY,
	.nwk_src = 0x0000,
	.nwk_dst = CHILD_ADDR,
	.nwk_security = true,
};

static struct zzb_aps_cmd confirm_key_cmd(uint8_t status, uint64_t dst)
{
	return (struct zzb_aps_cmd){
		.id = ZZB_APS_CMD_CONFIRM_KEY,
		.confirm_key.status = status,
		.confirm_key.type = ZZB_APS_KEY_TC_LINK,
		.confirm_key.dst = dst,
	};
}

/* The node holds key B, not verified yet */
static struct zzb_aps_key_pair *install_key_b(void)
{
	authorize();
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	return zzb_aps_key_pair_find(node, TC_EUI64);
}

/* R22 4.4.8.2.3 */
ZTEST(zzb_aps_cmd, test_confirm_key)
{
	struct zzb_aps_key_pair *pair = install_key_b();
	struct zzb_aps_cmd cmd = confirm_key_cmd(0x00, NODE_EUI64);
	uint8_t rec[27];

	receive_cmd(&cmd, &confirm);
	zassert_equal(pair->attr, ZZB_APS_KEY_VERIFIED);
	zassert_equal(pair->in_fc, 0);
	zassert_equal(settings_load_one("zzb/0/aps/key/0", rec, sizeof(rec)), sizeof(rec));
	zassert_equal(rec[9], ZZB_APS_KEY_VERIFIED);

	zassert_equal(zzb_apsme_confirm_key_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_apsme_confirm_key_ind_fake.arg0_val, node);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.arg1_val, 0x00);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.arg2_val, TC_EUI64);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.arg3_val, ZZB_APS_KEY_TC_LINK);
}

ZTEST(zzb_aps_cmd, test_confirm_key_with_a_failure_status)
{
	struct zzb_aps_key_pair *pair = install_key_b();
	struct zzb_aps_cmd cmd = confirm_key_cmd(0xad, NODE_EUI64);

	receive_cmd(&cmd, &confirm);
	zassert_equal(pair->attr, ZZB_APS_KEY_UNVERIFIED);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.call_count, 1);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.arg1_val, 0xad);
}

ZTEST(zzb_aps_cmd, test_rejected_confirm_keys)
{
	struct zzb_aps_key_pair *pair = install_key_b();
	struct zzb_aps_cmd cmd = confirm_key_cmd(0x00, NODE_EUI64);
	struct tc_frame how = confirm;

	/* R22 4.4.8.2.3 step 1 */
	how.nwk_dst = 0xfffd;
	receive_cmd(&cmd, &how);

	/* R22 Table 4-6: APS security is required */
	how = confirm;
	how.flags = 0;
	receive_cmd(&cmd, &how);

	cmd = confirm_key_cmd(0x00, 0x0011223344556677ULL);
	receive_cmd(&cmd, &confirm);

	zassert_equal(pair->attr, ZZB_APS_KEY_UNVERIFIED);
	zassert_equal(zzb_apsme_confirm_key_ind_fake.call_count, 0);
}

ZTEST_SUITE(zzb_aps_cmd, NULL, setup, before, after, NULL);
