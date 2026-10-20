/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/poll_control.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/fc.h"
#include "common/node.h"
#include "common/queue.h"
#include "fake_radio.h"
#include "nwk/nwk.h"
#include "parent.h"
#include "storage/settings.h"
#include "zcl/zcl.h"
#include "zdo/zdo.h"

#define PARENT_ADDR  0x0000
#define PARENT_EUI64 0x00124b0000000001ULL
#define CHILD_ADDR   0x1234

/* The zzb node on fake radio 1 */
static struct zzb_node *child;

/* The events of the BDB callback */
static struct {
	int count;
	enum zzb_bdb_event event;
	enum zzb_bdb_status status;
} events;

static void record_event(struct zzb_node *node, enum zzb_bdb_event event,
			 enum zzb_bdb_status status)
{
	events.count++;
	events.event = event;
	events.status = status;
}

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "sleepy",
	.power_source = 0x03,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;

static struct zzb_zcl_ias_zone zone = {
	.zone_type = 0x0015,
};

static struct zzb_zcl_poll_control poll_control;

/* A battery powered contact switch, with Poll Control on the first endpoint as zigpy needs it */
static const struct zzb_zcl_cluster sensor[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_POLL_CONTROL_SERVER(&poll_control),
	ZZB_ZCL_IAS_ZONE_SERVER(&zone),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0402,
		.clusters = sensor,
		.cluster_count = ARRAY_SIZE(sensor),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_DISPOSABLE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The read of Basic ManufacturerName and ModelIdentifier at the interview of zigpy */
static const uint8_t read[] = {0x00, 0x10, 0x00, 0x04, 0x00, 0x05, 0x00};

static void check_event(enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	zassert_equal(events.count, 1);
	zassert_equal(events.event, event);
	zassert_equal(events.status, status);
}

/*
 * Discovery, association, then the network key, the trust center link key exchange and the End
 * Device Timeout Response, each fetched by a poll
 */
static void steer(void)
{
	zassert_ok(zzb_bdb_steering(child));
	k_sleep(K_SECONDS(5));
	check_event(ZZB_BDB_EVENT_STEERING, ZZB_BDB_SUCCESS);
	events.count = 0;
}

/* Waits until the parent has count ZCL frames of the child; returns the milliseconds waited */
static int64_t wait_zcl_frames(int count)
{
	int64_t start = k_uptime_get();

	while (parent_log.zcl_frames < count && k_uptime_get() - start < 8000) {
		k_sleep(K_MSEC(10));
	}

	zassert_true(parent_log.zcl_frames >= count);
	return k_uptime_get() - start;
}

struct reboot_call {
	struct k_work work;
	int err;
};

/* The layers above the MAC start again from their records, as zzb_node_init() does */
static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_nwk_init(child);
	zzb_aps_init(child);
	zzb_zdo_init(child);
	zzb_zcl_init(child);
	call->err = zzb_settings_load(child);
	if (call->err == 0) {
		call->err = zzb_fc_start(&child->nwk.fc);
	}

	if (call->err == 0) {
		call->err = zzb_fc_start(&child->aps.fc);
	}

	zzb_zcl_start(child);
	zzb_bdb_init(child);
}

static void reboot(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	zassert_ok(call.err);
	events.count = 0;
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.radio = fake_radios[1],
		.device = &device,
		.bdb_cb = record_event,
		.sleepy = true,
	};

	fake_radio_reset();
	zassert_ok(zzb_node_init(&cfg, &child));
	return NULL;
}

/* Every test starts with a node off a network, since ztest orders tests by name */
static void before(void *fixture)
{
	zassert_ok(zzb_bdb_reset(child));
	k_sleep(K_MSEC(100));
	parent_start(PARENT_ADDR, PARENT_EUI64, CHILD_ADDR, ZZB_NWK_PARENT_POLL_KEEPALIVE);
	parent_send_key_after_join(true);
	events.count = 0;
}

static void after(void *fixture)
{
	zassert_equal(parent_log.expired, 0);
}

/* BDB 8.2 with the trust center link key exchange of BDB 10.2.5, all replies indirect */
ZTEST(zzb_sleepy_integration, test_steering)
{
	const struct zzb_aps_key_pair *pair;

	steer();
	zassert_equal(child->nwk.nib.capability, ZZB_NWK_CAP_ALLOC_ADDR);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
	zassert_true(parent_log.polls > 0);
	zassert_equal(parent_log.device_annces, 1);
	zassert_equal(parent_log.node_desc_reqs, 1);
	zassert_equal(parent_log.permit_joining_reqs, 1);

	pair = zzb_aps_key_pair_find(child, PARENT_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, tc_link_key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_VERIFIED);

	/* The End Device Timeout Response came through a poll */
	zassert_equal(parent_log.ed_timeout_reqs, 1);
	zassert_equal(child->nwk.nib.parent_info, ZZB_NWK_PARENT_POLL_KEEPALIVE);
	zassert_false(fake_radio_rx_on(1));
}

/* BDB 6.8: one poll per long interval while nothing is expected */
ZTEST(zzb_sleepy_integration, test_idle_node_polls_at_the_long_interval)
{
	int polls;

	steer();
	polls = parent_log.polls;
	k_sleep(K_SECONDS(20));
	zassert_between_inclusive(parent_log.polls - polls, 3, 5);
}

/*
 * The interview of zigpy sends each request after the response to the last one. The first one
 * waits for a poll at the long interval, the next ones arrive during the fast poll after a frame.
 */
ZTEST(zzb_sleepy_integration, test_requests_after_a_frame_arrive_fast)
{
	uint8_t req[sizeof(read)];

	steer();
	memcpy(req, read, sizeof(read));
	parent_send_zcl(ZZB_ZCL_BASIC, req, sizeof(req));
	zassert_true(wait_zcl_frames(1) <= CONFIG_ZZB_NWK_LONG_POLL_MS + 100);
	zassert_equal(parent_log.zcl[0], 0x18);

	for (int i = 2; i <= 4; i++) {
		req[1]++;
		parent_send_zcl(ZZB_ZCL_BASIC, req, sizeof(req));
		zassert_true(wait_zcl_frames(i) <= CONFIG_ZZB_NWK_SHORT_POLL_MS + 100);
		zassert_equal(parent_log.zcl[1], req[1]);
	}
}

/* 802.15.4 5.1.6.3: frame pending in the first frame makes the node poll for the second at once */
ZTEST(zzb_sleepy_integration, test_frame_pending_fetches_the_next_frame)
{
	uint8_t second[sizeof(read)];
	int64_t first;

	steer();
	memcpy(second, read, sizeof(read));
	second[1]++;
	k_sleep(K_SECONDS(4));
	parent_send_zcl(ZZB_ZCL_BASIC, read, sizeof(read));
	parent_send_zcl(ZZB_ZCL_BASIC, second, sizeof(second));
	wait_zcl_frames(1);
	first = k_uptime_get();
	wait_zcl_frames(2);
	zassert_true(k_uptime_get() - first <= 100);
	zassert_equal(parent_log.zcl[1], second[1]);
}

/* Only deadline timers are armed, and the receiver is off (section 20 of doc/architecture.md) */
ZTEST(zzb_sleepy_integration, test_idle_node_arms_only_deadline_timers)
{
	steer();
	k_sleep(K_SECONDS(4));

	zassert_true(k_work_delayable_is_pending(&child->nwk.poll_timer));
	zassert_true(k_work_delayable_is_pending(&child->nwk.keepalive));
	zassert_true(k_work_delayable_is_pending(&child->zcl.poll_control_timer));

	zassert_false(k_work_delayable_is_pending(&child->mac.timer));
	zassert_false(k_work_delayable_is_pending(&child->nwk.timer));
	zassert_false(k_work_delayable_is_pending(&child->aps.timer));
	zassert_false(k_work_delayable_is_pending(&child->zdo.channel_timer));
	zassert_false(k_work_delayable_is_pending(&child->zdo.client_timer));
	zassert_false(k_work_delayable_is_pending(&child->bdb.timer));
	zassert_false(k_work_delayable_is_pending(&child->bdb.fb.timer));
	zassert_false(k_work_delayable_is_pending(&child->zcl.identify_timer));
	zassert_false(k_work_delayable_is_pending(&child->zcl.report_timer));
	zassert_false(k_work_delayable_is_pending(&child->zcl.ias_zone_timer));
	zassert_false(k_work_delayable_is_pending(&child->zcl.time_timer));
	zassert_equal(child->nwk.poll_holds, 0);
	zassert_false(fake_radio_rx_on(1));
}

/* BDB 7.1: the rejoin response comes through the poll at the end of macResponseWaitTime */
ZTEST(zzb_sleepy_integration, test_reboot_rejoins)
{
	steer();
	reboot();
	zassert_ok(zzb_bdb_start(child));
	k_sleep(K_SECONDS(3));

	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.rejoin_reqs, 1);
	zassert_true(parent_log.rejoin_hdr.flags & ZZB_NWK_SECURITY);
	zassert_equal(parent_log.device_annces, 2);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
}

/*
 * Three polls fail at the long interval; the secure rejoin on the old channel finds no beacon, the
 * one on all channels finds the parent
 */
ZTEST(zzb_sleepy_integration, test_parent_moves_to_another_channel)
{
	steer();
	parent_set_channel(20);
	k_sleep(K_SECONDS(20));

	check_event(ZZB_BDB_EVENT_REJOIN, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.rejoin_reqs, 1);
	zassert_equal(child->nwk.nib.channel, 20);
}

/* BDB 9.4: Mgmt_Leave_req waits at the parent for the next poll */
ZTEST(zzb_sleepy_integration, test_mgmt_leave_resets_the_node)
{
	steer();
	k_sleep(K_SECONDS(4));
	parent_send_mgmt_leave(false);
	k_sleep(K_MSEC(CONFIG_ZZB_NWK_LONG_POLL_MS + 1000));

	check_event(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.mgmt_leave_rsps, 1);
	zassert_equal(parent_log.leaves, 1);
	zassert_false(fake_radio_rx_on(1));
}

/*
 * zigpy binds Poll Control at the interview and writes FastPollTimeout 120. A Check-in after the
 * new Check-inInterval of 10 s gets the Check-in Response of zigpy with Start Fast Polling and
 * 120 quarter seconds, so the node polls at the short interval for 30 s. Check-inInterval 1 h
 * then keeps the next check-in out of the test.
 */
ZTEST(zzb_sleepy_integration, test_poll_control_check_in)
{
	static const uint8_t write_timeout[] = {0x00, 0x50, 0x02, 0x03, 0x00, 0x21, 0x78, 0x00};
	static const uint8_t write_interval[] = {0x00, 0x51, 0x02, 0x00, 0x00,
						 0x23, 0x28, 0x00, 0x00, 0x00};
	static const uint8_t write_hour[] = {0x00, 0x52, 0x02, 0x00, 0x00,
					     0x23, 0x40, 0x38, 0x00, 0x00};
	uint8_t check_in_rsp[] = {0x11, 0x00, 0x00, 0x01, 0x78, 0x00};
	uint8_t bind[22] = {0x24};
	int frames;
	int polls;

	steer();
	sys_put_le64(child->eui64, bind + 1);
	bind[9] = 1;
	sys_put_le16(ZZB_ZCL_POLL_CONTROL, bind + 10);
	bind[12] = ZZB_APS_ADDR_EXT;
	sys_put_le64(PARENT_EUI64, bind + 13);
	bind[21] = 1;
	parent_send_zdo(ZZB_ZDP_BIND_REQ, bind, sizeof(bind));
	parent_send_zcl(ZZB_ZCL_POLL_CONTROL, write_timeout, sizeof(write_timeout));
	parent_send_zcl(ZZB_ZCL_POLL_CONTROL, write_interval, sizeof(write_interval));
	wait_zcl_frames(2);
	zassert_equal(poll_control.fast_poll_timeout, 120);
	zassert_equal(poll_control.check_in_interval, 40);

	frames = parent_log.zcl_frames;
	k_sleep(K_MSEC(10 * MSEC_PER_SEC - 200));
	zassert_equal(parent_log.zcl_frames, frames);
	wait_zcl_frames(frames + 1);
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_POLL_CONTROL);
	zassert_equal(parent_log.zcl_len, 3);
	zassert_equal(parent_log.zcl[0], 0x19);
	zassert_equal(parent_log.zcl[2], ZZB_ZCL_POLL_CONTROL_CHECK_IN);

	check_in_rsp[1] = parent_log.zcl[1];
	parent_send_zcl(ZZB_ZCL_POLL_CONTROL, check_in_rsp, sizeof(check_in_rsp));
	parent_send_zcl(ZZB_ZCL_POLL_CONTROL, write_hour, sizeof(write_hour));
	k_sleep(K_SECONDS(2));
	zassert_equal(poll_control.check_in_interval, 0x3840);
	polls = parent_log.polls;
	k_sleep(K_SECONDS(20));
	zassert_true(parent_log.polls - polls >=
		     20 * MSEC_PER_SEC / CONFIG_ZZB_NWK_SHORT_POLL_MS - 2);

	k_sleep(K_SECONDS(10));
	polls = parent_log.polls;
	k_sleep(K_SECONDS(10));
	zassert_between_inclusive(parent_log.polls - polls, 1, 3);
}

ZTEST_SUITE(zzb_sleepy_integration, NULL, setup, before, after, NULL);
