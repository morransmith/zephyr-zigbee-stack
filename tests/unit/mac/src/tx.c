/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/mac.h"

static const uint8_t msdu[] = {0x42};

static void send(uint8_t handle)
{
	zassert_ok(zzb_mcps_data_req(node, msdu_buf(handle, msdu, sizeof(msdu))));
}

ZTEST(zzb_mac_tx, test_data_frame_gets_mac_header)
{
	/* Not associated: extended source address and PAN ID 0xffff (802.15.4 5.1.6.1) */
	static const uint8_t unassociated[] = {0x21, 0xc8, 0x00, 0x62, 0x1a, 0x00,
					       0x00, 0xff, 0xff, 0x77, 0x66, 0x55,
					       0x44, 0x33, 0x22, 0x11, 0x00, 0x42};
	static const uint8_t associated[] = {0x61, 0x88, 0x00, 0x62, 0x1a,
					     0x00, 0x00, 0x78, 0x56, 0x42};

	send(7);
	run_stack();
	node->mac.pan_id = PAN_ID;
	node->mac.short_addr = 0x5678;
	send(8);
	run_stack();

	check_sent(0, unassociated, sizeof(unassociated));
	check_sent(1, associated, sizeof(associated));
	zassert_equal((uint8_t)(sent[0].data[2] + 1), sent[1].data[2]);
	zassert_equal(zzb_mcps_data_conf_fake.call_count, 2);
	zassert_equal_ptr(zzb_mcps_data_conf_fake.arg0_history[0], node);
	zassert_equal(zzb_mcps_data_conf_fake.arg1_history[0], 7);
	zassert_equal(zzb_mcps_data_conf_fake.arg2_history[0], ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_mcps_data_conf_fake.arg1_history[1], 8);
}

ZTEST(zzb_mac_tx, test_no_ack_is_retried_with_same_sequence_number)
{
	parent.lost = 4;

	send(1);
	run_stack();

	/* macMaxFrameRetries is 3 */
	zassert_equal(sent_count, 4);
	for (int i = 1; i < sent_count; i++) {
		zassert_mem_equal(sent[i].data, sent[0].data, sent[0].len);
	}
	zassert_equal(zzb_mcps_data_conf_fake.call_count, 1);
	zassert_equal(zzb_mcps_data_conf_fake.arg2_val, ZZB_MAC_STATUS_NO_ACK);
}

ZTEST(zzb_mac_tx, test_retries_stop_at_ack)
{
	parent.lost = 1;

	send(1);
	run_stack();

	zassert_equal(sent_count, 2);
	zassert_equal(zzb_mcps_data_conf_fake.call_count, 1);
	zassert_equal(zzb_mcps_data_conf_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
}

ZTEST(zzb_mac_tx, test_channel_access_failure_is_not_retried)
{
	parent.busy = true;

	send(1);
	run_stack();

	zassert_equal(sent_count, 1);
	zassert_equal(zzb_mcps_data_conf_fake.call_count, 1);
	zassert_equal(zzb_mcps_data_conf_fake.arg2_val, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
}

ZTEST(zzb_mac_tx, test_frames_are_sent_in_order)
{
	send(1);
	send(2);
	send(3);
	run_stack();

	zassert_equal(sent_count, 3);
	for (int i = 1; i < sent_count; i++) {
		zassert_equal((uint8_t)(sent[i - 1].data[2] + 1), sent[i].data[2]);
	}
	zassert_equal(zzb_mcps_data_conf_fake.call_count, 3);
	for (int i = 0; i < 3; i++) {
		zassert_equal(zzb_mcps_data_conf_fake.arg1_history[i], i + 1);
	}
}

ZTEST(zzb_mac_tx, test_oversized_frame_is_rejected)
{
	static const uint8_t long_msdu[103];
	struct net_buf *no_headroom = msdu_buf(1, msdu, sizeof(msdu));
	struct net_buf *too_long = msdu_buf(2, long_msdu, sizeof(long_msdu));

	net_buf_push(no_headroom, net_buf_headroom(no_headroom));
	/* 23-byte header: extended destination in another PAN, extended source */
	zzb_frame_meta(too_long)->dst = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_EXT,
		.pan_id = PAN_ID,
		.ext_addr = 0x0102030405060708ULL,
	};

	zassert_equal(zzb_mcps_data_req(node, no_headroom), -EMSGSIZE);
	zassert_equal(zzb_mcps_data_req(node, too_long), -EMSGSIZE);
	run_stack();

	zassert_equal(sent_count, 0);
	net_buf_unref(no_headroom);
	net_buf_unref(too_long);
}

ZTEST_SUITE(zzb_mac_tx, NULL, mac_setup, mac_before, mac_after, NULL);
