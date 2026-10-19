/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "sec/ccm.h"

/* Command header with security, then a Switch Key command */
static const uint8_t cmd_hdr[] = {0x21, 0x07};
static const uint8_t payload[] = {0x09, 0x01};

static struct zzb_node *node;
static struct zzb_node *tc;

static struct net_buf *frame_buf(const uint8_t *frame, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_APS_AUX_MAX_LEN);
	net_buf_add_mem(buf, frame, len);
	return buf;
}

/* The trust center secures the Switch Key command with link_key */
static struct net_buf *secured_cmd(const uint8_t *link_key, enum zzb_sec_key_id key_id,
				   bool ext_nonce)
{
	struct net_buf *buf = frame_buf(cmd_hdr, sizeof(cmd_hdr));

	net_buf_add_mem(buf, payload, sizeof(payload));
	zassert_ok(zzb_aps_secure(tc, buf, sizeof(cmd_hdr), link_key, key_id, ext_nonce));
	return buf;
}

static void check_plaintext(struct net_buf *buf)
{
	zassert_equal(buf->len, sizeof(cmd_hdr) + sizeof(payload));
	zassert_mem_equal(buf->data, cmd_hdr, sizeof(cmd_hdr));
	zassert_mem_equal(buf->data + sizeof(cmd_hdr), payload, sizeof(payload));
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
}

/*
 * The zigbee-rs Transport Key of tests/unit/sec (see NOTICE), received while the network key is
 * pending, is secured with the key-transport key of the default trust center link key
 */
ZTEST(zzb_aps_security, test_captured_transport_key)
{
	static const uint8_t frame[] = {
		0x21, 0x95, 0x30, 0x00, 0x00, 0x00, 0x00, 0xe1, 0x52, 0x38, 0x7d, 0xc1, 0x36, 0xce,
		0xf4, 0xcc, 0x56, 0x50, 0x5e, 0x07, 0x2d, 0xc5, 0xc1, 0xe8, 0x40, 0xf2, 0xd5, 0xce,
		0x0c, 0xa9, 0x2d, 0x64, 0x23, 0xcc, 0x0c, 0x56, 0xcc, 0xc4, 0xcc, 0x0f, 0x18, 0xa2,
		0xe4, 0x82, 0x88, 0x58, 0x4a, 0x90, 0x3e, 0x00, 0x47, 0x60, 0xf2, 0x5d,
	};
	static const uint8_t key[] = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89};
	struct net_buf *buf = frame_buf(frame, sizeof(frame));
	struct zzb_aps_sec sec;

	zzb_nwk_key_pending_fake.return_val = true;
	zassert_ok(zzb_aps_unsecure(node, buf, 2, 0x0000, &sec));
	zassert_equal(sec.key_id, ZZB_SEC_KEY_TRANSPORT);
	zassert_equal(sec.src_addr, TC_EUI64);
	zassert_is_null(sec.pair);

	/* APS header, Transport Key, network key type, key */
	zassert_equal(buf->len, 2 + 35);
	zassert_equal(buf->data[0], 0x21);
	zassert_equal(buf->data[2], 0x05);
	zassert_equal(buf->data[3], 0x01);
	zassert_mem_equal(buf->data + 4, key, sizeof(key));
	net_buf_unref(buf);
}

ZTEST(zzb_aps_security, test_default_key_only_while_the_network_key_is_pending)
{
	struct net_buf *buf = secured_cmd(zzb_aps_default_tc_link_key, ZZB_SEC_KEY_TRANSPORT, true);
	struct zzb_aps_sec sec;

	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -ENOENT);
	net_buf_unref(buf);
}

ZTEST(zzb_aps_security, test_link_key_round_trip)
{
	uint32_t fc = tc->aps.fc.value;
	struct net_buf *buf;
	struct zzb_aps_sec sec;
	struct zzb_sec_aux_hdr aux;

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	buf = secured_cmd(key_b, ZZB_SEC_KEY_DATA, true);
	zassert_equal(tc->aps.fc.value, fc + 1);

	/* R22 4.4.1.1: the security level sub-field is 0 on air */
	zassert_equal(zzb_sec_aux_hdr_parse(buf->data + sizeof(cmd_hdr), buf->len, &aux),
		      ZZB_APS_AUX_MAX_LEN);
	zassert_equal(buf->data[sizeof(cmd_hdr)] & ZZB_SEC_CTRL_LEVEL, 0);
	zassert_equal(aux.key_id, ZZB_SEC_KEY_DATA);
	zassert_equal(aux.frame_counter, fc);
	zassert_equal(aux.src_addr, TC_EUI64);
	zassert_equal(buf->len,
		      sizeof(cmd_hdr) + ZZB_APS_AUX_MAX_LEN + sizeof(payload) + ZZB_SEC_MIC_LEN);

	zassert_ok(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x1234, &sec));
	check_plaintext(buf);
	zassert_equal(sec.key_id, ZZB_SEC_KEY_DATA);
	zassert_equal(sec.src_addr, TC_EUI64);
	zassert_equal_ptr(sec.pair, zzb_aps_key_pair_find(node, TC_EUI64));
	zassert_equal(sec.pair->in_fc, fc + 1);
	net_buf_unref(buf);
}

/* R22 4.4.1.2 step 4 checks the frame counter of unique link keys only */
ZTEST(zzb_aps_security, test_stale_frame_counter)
{
	struct net_buf *buf;
	struct zzb_aps_sec sec;

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	buf = secured_cmd(key_b, ZZB_SEC_KEY_DATA, true);
	zzb_aps_key_pair_find(node, TC_EUI64)->in_fc = tc->aps.fc.value;
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -EALREADY);
	net_buf_unref(buf);

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_PROVISIONAL,
					  ZZB_APS_LINK_KEY_GLOBAL));
	buf = secured_cmd(key_b, ZZB_SEC_KEY_DATA, true);
	zzb_aps_key_pair_find(node, TC_EUI64)->in_fc = tc->aps.fc.value;
	zassert_ok(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec));
	net_buf_unref(buf);
}

/* Without nwkAddressMap only the trust center at 0x0000 is known (R22 4.6.2.2) */
ZTEST(zzb_aps_security, test_without_extended_nonce)
{
	struct net_buf *buf;
	struct zzb_aps_sec sec;

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	buf = secured_cmd(zzb_aps_default_tc_link_key, ZZB_SEC_KEY_DATA, false);
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x1234, &sec), -ENOENT);
	zassert_ok(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec));
	check_plaintext(buf);
	zassert_equal(sec.src_addr, TC_EUI64);
	net_buf_unref(buf);
}

/*
 * Z-Stack Home 1.2 sends the Transport Key without the extended nonce; the parent is the sender,
 * known from the association
 */
ZTEST(zzb_aps_security, test_transport_key_without_extended_nonce)
{
	struct zzb_nwk_neighbor parent = {
		.in_use = true,
		.ext_addr = TC_EUI64,
		.short_addr = 0x0000,
		.relationship = ZZB_NWK_REL_PARENT,
	};
	struct net_buf *buf;
	struct zzb_aps_sec sec;

	zzb_nwk_key_pending_fake.return_val = true;
	buf = secured_cmd(zzb_aps_default_tc_link_key, ZZB_SEC_KEY_TRANSPORT, false);
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -ENOENT);

	zzb_nwk_neighbor_find_short_fake.return_val = &parent;
	zassert_ok(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec));
	zassert_equal(zzb_nwk_neighbor_find_short_fake.arg1_val, 0x0000);
	check_plaintext(buf);
	zassert_equal(sec.src_addr, TC_EUI64);
	zassert_is_null(sec.pair);
	net_buf_unref(buf);
}

ZTEST(zzb_aps_security, test_rejected_frames)
{
	struct net_buf *buf;
	struct zzb_aps_sec sec;

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));

	/* The key of the entry is not the key of the frame */
	buf = secured_cmd(key_b, ZZB_SEC_KEY_DATA, true);
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -EBADMSG);
	net_buf_unref(buf);

	/* The network key identifier belongs to NWK */
	buf = secured_cmd(zzb_aps_default_tc_link_key, ZZB_SEC_KEY_DATA, true);
	buf->data[sizeof(cmd_hdr)] |= FIELD_PREP(ZZB_SEC_CTRL_KEY_ID, ZZB_SEC_KEY_NWK);
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -EINVAL);
	net_buf_unref(buf);

	buf = frame_buf(cmd_hdr, sizeof(cmd_hdr));
	zassert_equal(zzb_aps_unsecure(node, buf, sizeof(cmd_hdr), 0x0000, &sec), -EINVAL);
	net_buf_unref(buf);
}

ZTEST(zzb_aps_security, test_secure_needs_room)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	net_buf_reserve(buf, 5 + 8 - 1);
	net_buf_add_mem(buf, cmd_hdr, sizeof(cmd_hdr));
	zassert_equal(zzb_aps_secure(tc, buf, sizeof(cmd_hdr), key_b, ZZB_SEC_KEY_DATA, true),
		      -EMSGSIZE);
	net_buf_unref(buf);
}

ZTEST_SUITE(zzb_aps_security, NULL, setup, before, NULL, NULL);
