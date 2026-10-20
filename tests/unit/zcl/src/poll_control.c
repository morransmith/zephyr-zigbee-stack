/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/poll_control.h>

#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/*
 * A third node, whose check-ins stay out of the other suites. Endpoint 160 has a Poll Control
 * server with Check-inIntervalMin 10 s, LongPollIntervalMin 2 s and FastPollTimeoutMax 60 s, and
 * sets the poll intervals of the node. Endpoint 161 has a second server without limits.
 */
#define POLL_EUI64 0x00124b0001020306ULL
#define POLL_EP    160
#define SECOND_EP  161

static struct zzb_zcl_poll_control poll160 = {
	.check_in_interval_min = 40,
	.long_poll_interval_min = 8,
	.fast_poll_timeout_max = 240,
};

static struct zzb_zcl_poll_control poll161;

static const struct zzb_zcl_cluster ep160_clusters[] = {
	ZZB_ZCL_POLL_CONTROL_SERVER(&poll160),
};

static const struct zzb_zcl_cluster ep161_clusters[] = {
	ZZB_ZCL_POLL_CONTROL_SERVER(&poll161),
};

static const struct zzb_endpoint poll_endpoints[] = {
	{
		.id = POLL_EP,
		.profile = 0x0104,
		.device_id = 0x0402,
		.clusters = ep160_clusters,
		.cluster_count = ARRAY_SIZE(ep160_clusters),
	},
	{
		.id = SECOND_EP,
		.profile = 0x0104,
		.device_id = 0x0402,
		.clusters = ep161_clusters,
		.cluster_count = ARRAY_SIZE(ep161_clusters),
	},
};

static const struct zzb_device poll_device = {
	.power_source = ZZB_POWER_DISPOSABLE,
	.endpoints = poll_endpoints,
	.endpoint_count = ARRAY_SIZE(poll_endpoints),
};

static struct zzb_node *poll_node;

/* Passes a frame of Poll Control from the requester to endpoint ep of the third node */
static void receive_poll(uint8_t ep, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ep,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_POLL_CONTROL,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(poll_node, buf, &ind);
}

struct reboot_call {
	struct k_work work;
	int err;
};

static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_zcl_init(poll_node);
	call->err = zzb_settings_load(poll_node);
	if (call->err == 0) {
		zzb_zcl_start(poll_node);
	}
}

/* Starts ZCL of the third node again from its records, as reboot() */
static int reboot_poll(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void reset_handler(struct k_work *work)
{
	zzb_zcl_reset(poll_node);
}

/* Resets the ZCL attributes of the third node, as reset_node() */
static void reset_poll(void)
{
	struct k_work work;

	zzb_call_sync(&work, reset_handler);
}

static uint8_t tsn;

/*
 * Sends the command id with the payload to endpoint ep after reset_fakes() and checks its Default
 * Response
 */
static void command(uint8_t ep, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x18, ++tsn, 0x0b, id, status};
	uint8_t frame[8] = {0x01, tsn, id};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	reset_fakes();
	receive_poll(ep, frame, 3 + len);
	check_frame(0, ep, ZZB_ZCL_POLL_CONTROL, rsp, sizeof(rsp));
}

static void set_long(uint8_t ep, uint32_t value, uint8_t status)
{
	const uint8_t payload[] = {value, value >> 8, value >> 16, value >> 24};

	command(ep, ZZB_ZCL_POLL_CONTROL_SET_LONG_POLL_INTERVAL, payload, sizeof(payload), status);
}

static void set_short(uint8_t ep, uint16_t value, uint8_t status)
{
	const uint8_t payload[] = {value, value >> 8};

	command(ep, ZZB_ZCL_POLL_CONTROL_SET_SHORT_POLL_INTERVAL, payload, sizeof(payload), status);
}

/*
 * Writes the attribute records to endpoint ep after reset_fakes() and checks the Write Attributes
 * Response
 */
static void write_attrs(uint8_t ep, const uint8_t *records, size_t len, const uint8_t *statuses,
			size_t statuses_len)
{
	uint8_t frame[32] = {0x00, ++tsn, 0x02};
	uint8_t rsp[16] = {0x18, tsn, 0x04};

	zassert_true(3 + len <= sizeof(frame));
	zassert_true(3 + statuses_len <= sizeof(rsp));
	memcpy(frame + 3, records, len);
	memcpy(rsp + 3, statuses, statuses_len);
	reset_fakes();
	receive_poll(ep, frame, 3 + len);
	check_frame(0, ep, ZZB_ZCL_POLL_CONTROL, rsp, 3 + statuses_len);
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = POLL_EUI64,
		.device = &poll_device,
	};

	test_node();
	zassert_ok(zzb_node_init(&cfg, &poll_node));
	return NULL;
}

/* Each test starts with the defaults, without records */
static void before(void *fixture)
{
	reset_poll();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static void stop_handler(struct k_work *work)
{
	zzb_zcl_init(poll_node);
}

/* zzb_zcl_init() stops the check-ins of the third node before the other suites */
static void teardown(void *fixture)
{
	struct k_work work;

	zzb_call_sync(&work, stop_handler);
}

/*
 * ZCL Table 3-132: Check-inInterval 1 h, LongPollInterval and ShortPollInterval from the poll
 * intervals of NWK, FastPollTimeout 10 s, and the limits of the application
 */
ZTEST(zzb_zcl_poll_control, test_defaults)
{
	static const uint8_t read[] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
				       0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0xfd, 0xff};
	static const uint8_t rsp[] = {0x18, 0x40, 0x01, 0x00, 0x00, 0x00, 0x23, 0x40, 0x38, 0x00,
				      0x00, 0x01, 0x00, 0x00, 0x23, 0x14, 0x00, 0x00, 0x00, 0x02,
				      0x00, 0x00, 0x21, 0x02, 0x00, 0x03, 0x00, 0x00, 0x21, 0x28,
				      0x00, 0x04, 0x00, 0x00, 0x23, 0x28, 0x00, 0x00, 0x00, 0x05,
				      0x00, 0x00, 0x23, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
				      0x21, 0xf0, 0x00, 0xfd, 0xff, 0x00, 0x21, 0x03, 0x00};

	zassert_ok(reboot_poll());
	receive_poll(POLL_EP, read, sizeof(read));
	check_frame(0, POLL_EP, ZZB_ZCL_POLL_CONTROL, rsp, sizeof(rsp));
}

/*
 * ZCL 3.16.4.1: Check-inInterval above 0x6E0000, below Check-inIntervalMin or below
 * LongPollInterval and FastPollTimeout of 0 or above FastPollTimeoutMax get INVALID_VALUE, and the
 * poll intervals are read-only. Check-inInterval 0 turns the check-ins off and is accepted.
 */
ZTEST(zzb_zcl_poll_control, test_write_checks)
{
	static const uint8_t outside[] = {0x00, 0x00, 0x23, 0x01, 0x00, 0x6e, 0x00, 0x03,
					  0x00, 0x21, 0x00, 0x00, 0x01, 0x00, 0x23, 0x30,
					  0x00, 0x00, 0x00, 0x02, 0x00, 0x21, 0x04, 0x00};
	static const uint8_t outside_rsp[] = {0x87, 0x00, 0x00, 0x87, 0x03, 0x00,
					      0x88, 0x01, 0x00, 0x88, 0x02, 0x00};
	static const uint8_t limits[] = {0x00, 0x00, 0x23, 0x27, 0x00, 0x00,
					 0x00, 0x03, 0x00, 0x21, 0xf1, 0x00};
	static const uint8_t limits_rsp[] = {0x87, 0x00, 0x00, 0x87, 0x03, 0x00};
	static const uint8_t at_limits[] = {0x00, 0x00, 0x23, 0x28, 0x00, 0x00,
					    0x00, 0x03, 0x00, 0x21, 0xf0, 0x00};
	static const uint8_t below_long[] = {0x00, 0x00, 0x23, 0x13, 0x00, 0x00, 0x00};
	static const uint8_t no_limits[] = {0x00, 0x00, 0x23, 0x14, 0x00, 0x00,
					    0x00, 0x03, 0x00, 0x21, 0xff, 0xff};
	static const uint8_t off[] = {0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00};

	write_attrs(POLL_EP, outside, sizeof(outside), outside_rsp, sizeof(outside_rsp));
	write_attrs(POLL_EP, limits, sizeof(limits), limits_rsp, sizeof(limits_rsp));
	zassert_equal(poll160.check_in_interval, 0x3840);
	zassert_equal(poll160.fast_poll_timeout, 0x28);

	write_attrs(POLL_EP, at_limits, sizeof(at_limits), (uint8_t[]){0x00}, 1);
	zassert_equal(poll160.check_in_interval, 0x28);
	zassert_equal(poll160.fast_poll_timeout, 0xf0);
	write_attrs(POLL_EP, off, sizeof(off), (uint8_t[]){0x00}, 1);
	zassert_equal(poll160.check_in_interval, 0);

	write_attrs(SECOND_EP, below_long, sizeof(below_long), (uint8_t[]){0x87, 0x00, 0x00}, 3);
	write_attrs(SECOND_EP, no_limits, sizeof(no_limits), (uint8_t[]){0x00}, 1);
	zassert_equal(poll161.check_in_interval, 0x14);
	zassert_equal(poll161.fast_poll_timeout, 0xffff);
}

/*
 * ZCL 3.16.5.5: Set Long Poll Interval outside 4 to 0x6E0000, below LongPollIntervalMin or
 * ShortPollInterval, or above a Check-inInterval other than 0 gets INVALID_VALUE
 */
ZTEST(zzb_zcl_poll_control, test_set_long_poll_interval)
{
	set_long(SECOND_EP, 3, ZZB_ZCL_INVALID_VALUE);
	set_long(SECOND_EP, 4, ZZB_ZCL_SUCCESS);
	zassert_equal(poll161.long_poll_interval, 4);

	set_long(POLL_EP, 7, ZZB_ZCL_INVALID_VALUE);
	set_long(POLL_EP, 0x3841, ZZB_ZCL_INVALID_VALUE);
	zassert_equal(poll160.long_poll_interval, 0x14);
	set_long(POLL_EP, 0x3840, ZZB_ZCL_SUCCESS);
	zassert_equal(poll160.long_poll_interval, 0x3840);

	set_long(SECOND_EP, 10, ZZB_ZCL_SUCCESS);
	set_short(SECOND_EP, 8, ZZB_ZCL_SUCCESS);
	set_long(SECOND_EP, 6, ZZB_ZCL_INVALID_VALUE);
	zassert_equal(poll161.long_poll_interval, 10);

	/* Check-inInterval 0 */
	write_attrs(SECOND_EP, (uint8_t[]){0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00}, 7,
		    (uint8_t[]){0x00}, 1);
	set_long(SECOND_EP, 0x6e0001, ZZB_ZCL_INVALID_VALUE);
	set_long(SECOND_EP, 0x6e0000, ZZB_ZCL_SUCCESS);
	zassert_equal(poll161.long_poll_interval, 0x6e0000);

	command(SECOND_EP, ZZB_ZCL_POLL_CONTROL_SET_LONG_POLL_INTERVAL,
		(uint8_t[]){0x10, 0x00, 0x00}, 3, ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 3.16.5.6: Set Short Poll Interval of 0 or above LongPollInterval gets INVALID_VALUE */
ZTEST(zzb_zcl_poll_control, test_set_short_poll_interval)
{
	set_short(POLL_EP, 0, ZZB_ZCL_INVALID_VALUE);
	set_short(POLL_EP, 0x15, ZZB_ZCL_INVALID_VALUE);
	zassert_equal(poll160.short_poll_interval, 0x02);
	set_short(POLL_EP, 0x14, ZZB_ZCL_SUCCESS);
	zassert_equal(poll160.short_poll_interval, 0x14);

	command(POLL_EP, ZZB_ZCL_POLL_CONTROL_SET_SHORT_POLL_INTERVAL, (uint8_t[]){0x10}, 1,
		ZZB_ZCL_MALFORMED_COMMAND);
}

/* Checks that the last call of zzb_nwk_set_poll_intervals() had these intervals in milliseconds */
static void check_intervals(uint32_t long_ms, uint32_t short_ms)
{
	zassert_true(zzb_nwk_set_poll_intervals_fake.call_count > 0);
	zassert_equal_ptr(zzb_nwk_set_poll_intervals_fake.arg0_val, poll_node);
	zassert_equal(zzb_nwk_set_poll_intervals_fake.arg1_val, long_ms);
	zassert_equal(zzb_nwk_set_poll_intervals_fake.arg2_val, short_ms);
}

/*
 * The server on endpoint 160, the first one, sets the poll intervals of NWK at the start, at every
 * change and at the reset of the node; the server on endpoint 161 does not
 */
ZTEST(zzb_zcl_poll_control, test_nwk_intervals)
{
	uint32_t long_poll = 0x30;

	zassert_ok(reboot_poll());
	zassert_equal(zzb_nwk_set_poll_intervals_fake.call_count, 1);
	check_intervals(5000, 500);

	set_long(POLL_EP, 0x28, ZZB_ZCL_SUCCESS);
	check_intervals(10000, 500);
	set_short(POLL_EP, 0x04, ZZB_ZCL_SUCCESS);
	check_intervals(10000, 1000);
	zassert_ok(zzb_zcl_attr_set(poll_node, POLL_EP, ZZB_ZCL_POLL_CONTROL,
				    ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL, &long_poll));
	check_intervals(12000, 1000);

	set_long(SECOND_EP, 0x20, ZZB_ZCL_SUCCESS);
	zassert_equal(zzb_nwk_set_poll_intervals_fake.call_count, 0);
	set_short(SECOND_EP, 0x03, ZZB_ZCL_SUCCESS);
	zassert_equal(zzb_nwk_set_poll_intervals_fake.call_count, 0);

	reset_poll();
	check_intervals(5000, 500);
}

/*
 * Check-inInterval, FastPollTimeout and the poll intervals are saved, and the reset of the node
 * restores the defaults
 */
ZTEST(zzb_zcl_poll_control, test_settings)
{
	static const uint8_t records[] = {0x00, 0x00, 0x23, 0x00, 0x01, 0x00,
					  0x00, 0x03, 0x00, 0x21, 0x50, 0x00};

	write_attrs(POLL_EP, records, sizeof(records), (uint8_t[]){0x00}, 1);
	set_long(POLL_EP, 0x30, ZZB_ZCL_SUCCESS);
	set_short(POLL_EP, 0x05, ZZB_ZCL_SUCCESS);
	zassert_ok(reboot_poll());
	zassert_equal(poll160.check_in_interval, 0x100);
	zassert_equal(poll160.long_poll_interval, 0x30);
	zassert_equal(poll160.short_poll_interval, 0x05);
	zassert_equal(poll160.fast_poll_timeout, 0x50);
	check_intervals(12000, 1250);

	reset_poll();
	zassert_equal(poll160.check_in_interval, 0x3840);
	zassert_equal(poll160.long_poll_interval, 0x14);
	zassert_equal(poll160.short_poll_interval, 0x02);
	zassert_equal(poll160.fast_poll_timeout, 0x28);
	zassert_ok(reboot_poll());
	zassert_equal(poll160.check_in_interval, 0x3840);
	zassert_equal(poll160.long_poll_interval, 0x14);
}

ZTEST_SUITE(zzb_zcl_poll_control, NULL, setup, before, after, teardown);
