/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "mac/mac.h"

#define PARENT_EUI64 0x0102030405060708ULL

static const struct zzb_mac_addr coord = {
	.mode = ZZB_MAC_ADDR_SHORT,
	.pan_id = PAN_ID,
	.short_addr = PARENT_SHORT,
};

/* R22 Table D-1, first form; capability: allocate address */
static const uint8_t assoc_req[] = {0x23, 0xc8, 0x00, 0x62, 0x1a, 0x00, 0x00, 0xff, 0xff, 0x77,
				    0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x01, 0x80};

/* R22 Table D-2, first form */
static const uint8_t data_req[] = {0x23, 0xc8, 0x00, 0x62, 0x1a, 0x00, 0x00, 0xff, 0xff,
				   0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x04};

/* R22 Table D-3, first form: short address 0x5678, association successful */
static const uint8_t assoc_rsp[] = {0x63, 0xcc, 0x10, 0x62, 0x1a, 0x77, 0x66, 0x55, 0x44,
				    0x33, 0x22, 0x11, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04,
				    0x03, 0x02, 0x01, 0x02, 0x78, 0x56, 0x00};

/* The same response with status PAN at capacity */
static const uint8_t assoc_rsp_full[] = {0x63, 0xcc, 0x10, 0x62, 0x1a, 0x77, 0x66, 0x55, 0x44,
					 0x33, 0x22, 0x11, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04,
					 0x03, 0x02, 0x01, 0x02, 0xff, 0xff, 0x01};

static void set_reply(const uint8_t *frame, size_t len)
{
	parent.pending = true;
	parent.reply = frame;
	parent.reply_len = len;
	parent.reply_to = ZZB_MAC_CMD_DATA_REQ;
}

ZTEST(zzb_mac_assoc, test_association_succeeds)
{
	/* Data frame with the short source address after association */
	static const uint8_t data[] = {0x61, 0x88, 0x00, 0x62, 0x1a, 0x00, 0x00, 0x78, 0x56, 0x42};
	static const uint8_t msdu[] = {0x42};

	set_reply(assoc_rsp, sizeof(assoc_rsp));

	zassert_ok(zzb_mlme_assoc_req(node, 15, &coord, 0x80));
	zassert_equal(zzb_mlme_scan_req(node, BIT(11), 0), -EBUSY);
	zassert_equal(zzb_mlme_poll_req(node, &coord), -EBUSY);
	zassert_equal(zzb_mlme_assoc_req(node, 15, &coord, 0x80), -EBUSY);

	/* The data request follows macResponseWaitTime, 491.52 ms */
	k_sleep(K_MSEC(480));
	zassert_equal(sent_count, 1);
	k_sleep(K_MSEC(40));

	check_sent(0, assoc_req, sizeof(assoc_req));
	zassert_equal(sent[0].radio.channel, 15);
	zassert_equal(sent[0].radio.pan_id, PAN_ID);
	check_sent(1, data_req, sizeof(data_req));
	zassert_equal(zzb_mlme_assoc_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_assoc_conf_fake.arg1_val, 0x5678);
	zassert_equal(zzb_mlme_assoc_conf_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(radio.short_addr, 0x5678);
	zassert_equal(node->mac.coord_short_addr, PARENT_SHORT);
	zassert_equal(node->mac.coord_ext_addr, PARENT_EUI64);

	zassert_ok(zzb_mcps_data_req(node, msdu_buf(1, msdu, sizeof(msdu))));
	run_stack();
	check_sent(2, data, sizeof(data));
}

ZTEST(zzb_mac_assoc, test_association_failures_reset_pan_id)
{
	/* lost = 4: the association request and its 3 retries (macMaxFrameRetries) get no ACK */
	static const struct {
		int lost;
		bool pending;
		const uint8_t *reply;
		enum zzb_mac_status status;
		int sent;
	} cases[] = {
		{.lost = 4, .status = ZZB_MAC_STATUS_NO_ACK, .sent = 4},
		{.status = ZZB_MAC_STATUS_NO_DATA, .sent = 2},
		{.pending = true, .status = ZZB_MAC_STATUS_NO_DATA, .sent = 2},
		{.pending = true,
		 .reply = assoc_rsp_full,
		 .status = ZZB_MAC_STATUS_PAN_AT_CAPACITY,
		 .sent = 2},
	};

	ARRAY_FOR_EACH_PTR(cases, c) {
		mac_before(NULL);
		set_reply(c->reply, sizeof(assoc_rsp_full));
		parent.pending = c->pending;
		parent.lost = c->lost;

		zassert_ok(zzb_mlme_assoc_req(node, 15, &coord, 0x80));
		k_sleep(K_MSEC(700));

		zassert_equal(zzb_mlme_assoc_conf_fake.call_count, 1, "case %d", c - cases);
		zassert_equal(zzb_mlme_assoc_conf_fake.arg1_val, 0xffff);
		zassert_equal(zzb_mlme_assoc_conf_fake.arg2_val, c->status, "case %d", c - cases);
		zassert_equal(node->mac.pan_id, 0xffff);
		zassert_equal(radio.pan_id, 0xffff);
		zassert_equal(radio.short_addr, 0xffff);
		zassert_equal(sent_count, c->sent, "case %d", c - cases);
		mac_after(NULL);
	}
}

ZTEST_SUITE(zzb_mac_assoc, NULL, mac_setup, mac_before, mac_after, NULL);
