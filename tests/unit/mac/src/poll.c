/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "mac/mac.h"

static const struct zzb_mac_addr coord = {
	.mode = ZZB_MAC_ADDR_SHORT,
	.pan_id = PAN_ID,
	.short_addr = PARENT_SHORT,
};

static void set_reply(const uint8_t *frame, size_t len)
{
	parent.pending = true;
	parent.reply = frame;
	parent.reply_len = len;
	parent.reply_to = ZZB_MAC_CMD_DATA_REQ;
}

static int call_index(void *fake)
{
	for (int i = 0; i < fff.call_history_idx; i++) {
		if (fff.call_history[i] == fake) {
			return i;
		}
	}

	return -1;
}

ZTEST(zzb_mac_poll, test_ack_without_frame_pending_is_no_data)
{
	/* Short source address of an associated device (802.15.4 5.3.4) */
	static const uint8_t data_req[] = {0x63, 0x88, 0x00, 0x62, 0x1a,
					   0x00, 0x00, 0x78, 0x56, 0x04};

	zassert_ok(zzb_mlme_poll_req(node, &coord));
	run_stack();

	check_sent(0, data_req, sizeof(data_req));
	zassert_equal(zzb_mlme_poll_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_poll_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_DATA);
}

ZTEST(zzb_mac_poll, test_pending_frame_is_confirmed_then_indicated)
{
	/* Data frame from the parent with frame pending set: more data is waiting */
	static const uint8_t frame[] = {0x71, 0x88, 0x10, 0x62, 0x1a, 0x78, 0x56, 0x00, 0x00, 0x42};

	set_reply(frame, sizeof(frame));

	zassert_ok(zzb_mlme_poll_req(node, &coord));
	run_stack();

	zassert_equal(zzb_mlme_poll_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_poll_conf_fake.arg1_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_mcps_data_ind_fake.call_count, 1);
	zassert_true(call_index(zzb_mlme_poll_conf) < call_index(zzb_mcps_data_ind));
	zassert_equal(data_ind.len, 1);
	zassert_equal(data_ind.data[0], 0x42);
	zassert_true(data_ind.meta.flags & ZZB_FRAME_PENDING);
}

ZTEST(zzb_mac_poll, test_empty_frame_is_no_data)
{
	static const uint8_t empty[] = {0x41, 0x88, 0x10, 0x62, 0x1a, 0x78, 0x56, 0x00, 0x00};

	set_reply(empty, sizeof(empty));

	zassert_ok(zzb_mlme_poll_req(node, &coord));
	run_stack();

	zassert_equal(zzb_mlme_poll_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_poll_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_DATA);
	zassert_equal(zzb_mcps_data_ind_fake.call_count, 0);
}

ZTEST(zzb_mac_poll, test_wait_ends_without_frame_from_coordinator)
{
	/* From 0x1234, not from the polled coordinator */
	static const uint8_t other[] = {0x41, 0x88, 0x10, 0x62, 0x1a, 0x78, 0x56, 0x34, 0x12, 0x42};

	parent.pending = true;

	zassert_ok(zzb_mlme_poll_req(node, &coord));
	run_stack();
	inject(other, sizeof(other));
	run_stack();

	/* macMaxFrameTotalWaitTime, 157.54 ms */
	k_sleep(K_MSEC(140));
	zassert_equal(zzb_mlme_poll_conf_fake.call_count, 0);
	k_sleep(K_MSEC(40));
	zassert_equal(zzb_mlme_poll_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_poll_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_DATA);
	zassert_equal(zzb_mcps_data_ind_fake.call_count, 1);
}

static void before(void *fixture)
{
	mac_before(fixture);
	node->mac.pan_id = PAN_ID;
	node->mac.short_addr = 0x5678;
}

ZTEST_SUITE(zzb_mac_poll, NULL, mac_setup, before, mac_after, NULL);
