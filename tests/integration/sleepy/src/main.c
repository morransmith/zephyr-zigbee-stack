/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "fake_radio.h"
#include "nwk/nwk.h"
#include "parent.h"
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

/* A battery powered contact switch */
static const struct zzb_zcl_cluster sensor[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
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

ZTEST_SUITE(zzb_sleepy_integration, NULL, setup, before, after, NULL);
