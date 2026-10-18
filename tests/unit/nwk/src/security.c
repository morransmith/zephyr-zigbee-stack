/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "nwk/nwk.h"
#include "sec/ccm.h"

/* NWK data frame from Ziggurat (see NOTICE), as in tests/unit/sec */
static const uint8_t nwk_key[ZZB_SEC_KEY_LEN] = {
	0xe8, 0x78, 0x5a, 0x1e, 0xd5, 0x99, 0x6b, 0x3e,
	0xf7, 0x15, 0xcb, 0x3f, 0xbd, 0xd6, 0x91, 0x87,
};
static const uint8_t frame[] = {
	0x08, 0x02, 0x42, 0x6b, 0x00, 0x00, 0x0f, 0x2e, 0x28, 0x7a, 0x0a, 0x00, 0x00,
	0xa8, 0xef, 0x17, 0x1e, 0x00, 0x4b, 0x12, 0x00, 0x00, 0xf7, 0xa7, 0xe3, 0x7b,
	0x47, 0xad, 0xb4, 0x75, 0x93, 0xc8, 0xa3, 0x75, 0xc9, 0x8b, 0xa6,
};
static const uint8_t payload[] = {
	0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0xa9, 0x01, 0x57, 0x01,
};
#define HDR_LEN       8
#define FRAME_COUNTER 0x0a7a
/* Offsets of auxiliary header fields in the frame */
#define SEC_CTRL_OFF  8
#define FC_OFF        9
#define KEY_SEQ_OFF   21

static struct zzb_node *node;
static struct zzb_node *sender;

static struct net_buf *frame_buf(const uint8_t *bytes, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NWK_AUX_LEN);
	net_buf_add_mem(buf, bytes, len);
	return buf;
}

/* Unsecures a copy of the frame with one byte changed */
static int unsecure_modified(size_t off, uint8_t value)
{
	struct net_buf *buf = frame_buf(frame, sizeof(frame));
	int err;

	buf->data[off] = value;
	err = zzb_nwk_unsecure(node, buf, HDR_LEN);
	net_buf_unref(buf);
	return err;
}

static void *setup(void)
{
	node = test_node(0);
	sender = test_node(1);
	return NULL;
}

static void before(void *fixture)
{
	zzb_nwk_init(node);
	zzb_nwk_init(sender);
	reset_fakes();
}

ZTEST(zzb_nwk_security, test_secure_gives_the_captured_frame)
{
	struct net_buf *buf = frame_buf(frame, HDR_LEN);

	net_buf_add_mem(buf, payload, sizeof(payload));
	zzb_nlme_set_nwk_key(sender, nwk_key, 0);
	sender->nwk.fc.value = FRAME_COUNTER;
	sender->nwk.fc.watermark = FRAME_COUNTER + CONFIG_ZZB_FRAME_COUNTER_JUMP;

	zassert_ok(zzb_nwk_secure(sender, buf, HDR_LEN));
	zassert_equal(buf->len, sizeof(frame));
	zassert_mem_equal(buf->data, frame, sizeof(frame));
	zassert_equal(sender->nwk.fc.value, FRAME_COUNTER + 1);
	net_buf_unref(buf);
}

ZTEST(zzb_nwk_security, test_secure_without_key)
{
	struct net_buf *buf = frame_buf(frame, HDR_LEN);

	zassert_equal(zzb_nwk_secure(sender, buf, HDR_LEN), -ENOENT);
	net_buf_unref(buf);
}

ZTEST(zzb_nwk_security, test_unsecure_the_captured_frame)
{
	struct zzb_nwk_neighbor *nb =
		zzb_nwk_neighbor_add(node, SENDER_EUI64, 0x0000, ZZB_NWK_REL_PARENT);
	struct net_buf *buf = frame_buf(frame, sizeof(frame));

	zzb_nlme_set_nwk_key(node, nwk_key, 0);

	zassert_ok(zzb_nwk_unsecure(node, buf, HDR_LEN));
	zassert_equal(buf->len, HDR_LEN + sizeof(payload));
	zassert_mem_equal(buf->data, frame, HDR_LEN);
	zassert_mem_equal(buf->data + HDR_LEN, payload, sizeof(payload));
	zassert_equal(nb->in_fc[0], FRAME_COUNTER + 1);
	net_buf_unref(buf);

	/* The same frame again */
	zassert_equal(unsecure_modified(0, frame[0]), -EALREADY);
}

ZTEST(zzb_nwk_security, test_unsecure_rejected)
{
	struct zzb_nwk_neighbor *nb;

	zzb_nlme_set_nwk_key(node, nwk_key, 0);
	zassert_equal(unsecure_modified(0, frame[0]), -ENOENT, "sender is not a neighbor");

	nb = zzb_nwk_neighbor_add(node, SENDER_EUI64, 0x0000, ZZB_NWK_REL_PARENT);
	zassert_equal(unsecure_modified(KEY_SEQ_OFF, 1), -ENOENT, "unknown key");
	zassert_equal(unsecure_modified(SEC_CTRL_OFF, 0x08), -EINVAL, "no extended nonce");
	zassert_equal(unsecure_modified(FC_OFF, 0x79), -EBADMSG);
	zassert_equal(unsecure_modified(HDR_LEN + ZZB_NWK_AUX_LEN, 0x00), -EBADMSG);
	zassert_equal(nb->in_fc[0], 0);

	nb->in_fc[0] = FRAME_COUNTER + 1;
	zassert_equal(unsecure_modified(FC_OFF, 0x79), -EALREADY, "older frame counter");
}

ZTEST(zzb_nwk_security, test_last_frame_counter_is_rejected)
{
	struct net_buf *buf = frame_buf(frame, sizeof(frame));

	zzb_nlme_set_nwk_key(node, nwk_key, 0);
	zzb_nwk_neighbor_add(node, SENDER_EUI64, 0x0000, ZZB_NWK_REL_PARENT);
	memset(buf->data + FC_OFF, 0xff, 4);

	zassert_equal(zzb_nwk_unsecure(node, buf, HDR_LEN), -EALREADY);
	net_buf_unref(buf);
}

/* R22 4.3.1.2 step 7 */
ZTEST(zzb_nwk_security, test_newer_key_becomes_active)
{
	static const uint8_t old_key[ZZB_SEC_KEY_LEN] = {0x01};
	struct net_buf *buf = frame_buf(frame, sizeof(frame));
	uint8_t rec[64];

	delete_nwk_records();
	zzb_nlme_set_ext_pan_id(node, 0x0102030405060708ULL);

	zzb_nwk_neighbor_add(node, SENDER_EUI64, 0x0000, ZZB_NWK_REL_PARENT);
	zzb_nlme_set_nwk_key(node, old_key, 0xff);
	zzb_nlme_set_nwk_key(node, nwk_key, 0);
	zassert_equal(node->nwk.nib.active_key, 0);

	zassert_ok(zzb_nwk_unsecure(node, buf, HDR_LEN));
	zassert_equal(node->nwk.nib.active_key, 1);
	net_buf_unref(buf);

	zassert_equal(load_record("nwk/keys", rec, sizeof(rec)), 39);
	zassert_equal(rec[1], 1, "active slot");
}

ZTEST(zzb_nwk_security, test_older_key_does_not_become_active)
{
	static const uint8_t new_key[ZZB_SEC_KEY_LEN] = {0x01};
	struct net_buf *buf = frame_buf(frame, sizeof(frame));

	zzb_nwk_neighbor_add(node, SENDER_EUI64, 0x0000, ZZB_NWK_REL_PARENT);
	zzb_nlme_set_nwk_key(node, nwk_key, 0);
	zzb_nlme_set_nwk_key(node, new_key, 1);
	node->nwk.nib.active_key = 1;

	zassert_ok(zzb_nwk_unsecure(node, buf, HDR_LEN));
	zassert_equal(node->nwk.nib.active_key, 1);
	net_buf_unref(buf);
}

ZTEST_SUITE(zzb_nwk_security, NULL, setup, before, NULL, NULL);
