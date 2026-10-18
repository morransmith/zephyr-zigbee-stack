/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/mac.h"

ZTEST(zzb_mac, test_init_binds_radio_and_programs_filters)
{
	zassert_equal_ptr(node->port.dev, radio_dev);
	zassert_equal(radio.ext_addr, NODE_EUI64);
	zassert_equal(radio.pan_id, 0xffff);
	zassert_equal(radio.short_addr, 0xffff);
	zassert_equal(radio.channel, 11);
	zassert_true(radio.started);
}

ZTEST(zzb_mac, test_init_without_radio_is_invalid)
{
	const struct zzb_node_config cfg = {
		.eui64 = NODE_EUI64,
	};
	struct zzb_node *other;

	zassert_equal(zzb_node_init(&cfg, &other), -EINVAL);
	zassert_is_null(other);
}

ZTEST(zzb_mac, test_data_frame_is_indicated)
{
	/* From the parent to 0x5678 with ACK request, one payload byte */
	static const uint8_t frame[] = {0x61, 0x88, 0x10, 0x62, 0x1a, 0x78, 0x56, 0x00, 0x00, 0x42};

	inject(frame, sizeof(frame));
	run_stack();

	zassert_equal(zzb_mcps_data_ind_fake.call_count, 1);
	zassert_equal_ptr(zzb_mcps_data_ind_fake.arg0_val, node);
	zassert_equal(data_ind.len, 1);
	zassert_equal(data_ind.data[0], 0x42);
	zassert_equal(data_ind.meta.src.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(data_ind.meta.src.pan_id, PAN_ID);
	zassert_equal(data_ind.meta.src.short_addr, PARENT_SHORT);
	zassert_equal(data_ind.meta.dst.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(data_ind.meta.dst.pan_id, PAN_ID);
	zassert_equal(data_ind.meta.dst.short_addr, 0x5678);
	zassert_equal(data_ind.meta.flags, ZZB_FRAME_ACK_REQ);
}

ZTEST(zzb_mac, test_unexpected_frames_are_dropped)
{
	static const uint8_t truncated[] = {0x61, 0x88};
	/* Beacon of the parent and association response, outside a scan and an association */
	static const uint8_t beacon[] = {0x00, 0x80, 0x10, 0x62, 0x1a, 0x00,
					 0x00, 0xff, 0xcf, 0x00, 0x00, 0x00};
	static const uint8_t assoc_rsp[] = {0x63, 0xcc, 0x10, 0x62, 0x1a, 0x77, 0x66, 0x55, 0x44,
					    0x33, 0x22, 0x11, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04,
					    0x03, 0x02, 0x01, 0x02, 0x78, 0x56, 0x00};
	static const uint8_t empty_data[] = {0x41, 0x88, 0x10, 0x62, 0x1a, 0x78, 0x56, 0x00, 0x00};

	inject(truncated, sizeof(truncated));
	inject(beacon, sizeof(beacon));
	inject(assoc_rsp, sizeof(assoc_rsp));
	inject(empty_data, sizeof(empty_data));
	run_stack();

	zassert_equal(zzb_mcps_data_ind_fake.call_count, 0);
}

ZTEST(zzb_mac, test_set_pan_id_and_short_address)
{
	static const uint8_t msdu[] = {0x42};
	/* Data frame from 0x5678 to the parent with ACK request */
	static const uint8_t frame[] = {0x61, 0x88, 0x00, 0x62, 0x1a, 0x00, 0x00, 0x78, 0x56, 0x42};

	zzb_mlme_set_pan_id(node, PAN_ID);
	zzb_mlme_set_short_addr(node, 0x5678);
	zassert_equal(radio.pan_id, PAN_ID);
	zassert_equal(radio.short_addr, 0x5678);

	zassert_ok(zzb_mcps_data_req(node, msdu_buf(1, msdu, sizeof(msdu))));
	run_stack();
	check_sent(0, frame, sizeof(frame));
}

ZTEST_SUITE(zzb_mac, NULL, mac_setup, mac_before, mac_after, NULL);
