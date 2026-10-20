/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "mac/mac.h"

/* Broadcast to PAN 0xffff, no source address (802.15.4 5.3.7) */
static const uint8_t beacon_req[] = {0x03, 0x08, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07};

/* Beacon of the parent with association permit, and the start of a Zigbee beacon payload */
static const uint8_t parent_beacon[] = {0x00, 0x80, 0x20, 0x62, 0x1a, 0x00, 0x00,
					0xff, 0xcf, 0x00, 0x00, 0x00, 0x22, 0x84};

ZTEST(zzb_mac_scan, test_scan_sends_beacon_request_per_channel)
{
	static const uint8_t channels[] = {11, 15, 20};

	node->mac.pan_id = PAN_ID;
	node->mac.channel = 25;

	zassert_ok(zzb_mlme_scan_req(node, BIT(11) | BIT(15) | BIT(20), 3));

	/* Three times aBaseSuperframeDuration * (2^3 + 1), 138.24 ms */
	k_sleep(K_MSEC(400));
	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 0);
	k_sleep(K_MSEC(80));
	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_scan_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_BEACON);

	zassert_equal(sent_count, ARRAY_SIZE(channels));
	ARRAY_FOR_EACH(channels, i) {
		check_sent(i, beacon_req, sizeof(beacon_req));
		zassert_equal(sent[i].radio.channel, channels[i]);
		zassert_equal(sent[i].radio.pan_id, 0xffff);
	}

	zassert_equal(radio.pan_id, PAN_ID);
	zassert_equal(radio.channel, 25);
}

ZTEST(zzb_mac_scan, test_beacon_is_notified_and_other_frames_dropped)
{
	static const uint8_t data[] = {0x41, 0x88, 0x10, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x42};

	parent.reply = parent_beacon;
	parent.reply_len = sizeof(parent_beacon);
	parent.reply_to = ZZB_MAC_CMD_BEACON_REQ;
	parent.reply_channel = 15;

	zassert_ok(zzb_mlme_scan_req(node, BIT(11) | BIT(15), 0));
	k_sleep(K_MSEC(10));
	inject(data, sizeof(data));
	k_sleep(K_MSEC(100));

	zassert_equal(zzb_mcps_data_ind_fake.call_count, 0);
	zassert_equal(zzb_mlme_beacon_notify_ind_fake.call_count, 1);
	zassert_equal(zzb_mlme_beacon_notify_ind_fake.arg2_val, 15);
	zassert_equal(zzb_mlme_beacon_notify_ind_fake.arg3_val, 0xcfff);
	zassert_equal(beacon.len, 3);
	zassert_mem_equal(beacon.data, parent_beacon + 11, 3);
	zassert_equal(beacon.meta.src.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(beacon.meta.src.pan_id, PAN_ID);
	zassert_equal(beacon.meta.src.short_addr, PARENT_SHORT);
	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_scan_conf_fake.arg1_val, ZZB_MAC_STATUS_SUCCESS);
}

ZTEST(zzb_mac_scan, test_channel_access_failure_skips_channel)
{
	parent.busy_channel = 11;

	zassert_ok(zzb_mlme_scan_req(node, BIT(11) | BIT(15), 3));

	/* Only channel 15 is listened on, for 138.24 ms */
	k_sleep(K_MSEC(120));
	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 0);
	k_sleep(K_MSEC(50));
	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 1);
	zassert_equal(zzb_mlme_scan_conf_fake.arg1_val, ZZB_MAC_STATUS_NO_BEACON);
	zassert_equal(sent_count, 2);
}

/* The scan starts the radio on every channel */
ZTEST(zzb_mac_scan, test_sleepy_node_receives_during_the_scan)
{
	mac_init_sleepy();

	zassert_ok(zzb_mlme_scan_req(node, BIT(11) | BIT(15), 0));
	k_sleep(K_MSEC(100));

	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 1);
	zassert_equal(sent_count, 2);
	for (int i = 0; i < sent_count; i++) {
		zassert_true(sent[i].radio.started);
		zassert_true(sent[i].radio.rx_on_when_idle);
	}

	zassert_equal(radio.channel, 11);
	zassert_false(radio.rx_on_when_idle);
	zassert_false(radio.started);
}

ZTEST(zzb_mac_scan, test_invalid_or_second_scan_is_rejected)
{
	zassert_equal(zzb_mlme_scan_req(node, BIT(11), 15), -EINVAL);
	zassert_equal(zzb_mlme_scan_req(node, BIT(10), 0), -EINVAL);
	zassert_equal(zzb_mlme_scan_req(node, 0, 0), -EINVAL);

	zassert_ok(zzb_mlme_scan_req(node, BIT(11), 0));
	zassert_equal(zzb_mlme_scan_req(node, BIT(11), 0), -EBUSY);
	k_sleep(K_MSEC(100));

	zassert_equal(zzb_mlme_scan_conf_fake.call_count, 1);
}

ZTEST_SUITE(zzb_mac_scan, NULL, mac_setup, mac_before, mac_after, NULL);
