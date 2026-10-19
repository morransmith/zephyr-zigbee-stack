/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"

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
};

/* The network key Transport Key of a trust center that the parent relays */
static const struct tc_frame transport = {
	.link_key = zzb_aps_default_tc_link_key,
	.key_id = ZZB_SEC_KEY_TRANSPORT,
	.flags = ZZB_APS_SECURITY,
	.nwk_src = ROUTER_ADDR,
	.nwk_dst = CHILD_ADDR,
};

static void receive_cmd(const struct zzb_aps_cmd *cmd, const struct tc_frame *how)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_CMD,
		.flags = how->flags,
		.counter = tc_counter++,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_APS_AUX_MAX_LEN);
	zzb_aps_hdr_write(&hdr, net_buf_add(buf, 2));
	net_buf_add(buf, zzb_aps_cmd_write(cmd, net_buf_tail(buf)));
	if (how->flags & ZZB_APS_SECURITY) {
		zassert_ok(zzb_aps_secure(tc, buf, 2, how->link_key, how->key_id, true));
	}

	zzb_nlde_data_ind(node, buf, how->nwk_dst, how->nwk_src, how->nwk_security);
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

ZTEST_SUITE(zzb_aps_cmd, NULL, setup, before, after, NULL);
