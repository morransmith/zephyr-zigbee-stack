/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
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

/* Basic, Identify and On/Off servers of an On/Off Light (Home Automation profile) */
static const struct zzb_zcl_cluster light[] = {
	{.id = 0x0000, .server = true},
	{.id = 0x0003, .server = true},
	{.id = 0x0006, .server = true},
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

static void check_event(enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	zassert_equal(events.count, 1);
	zassert_equal(events.event, event);
	zassert_equal(events.status, status);
}

/* Discovery on the four primary channels, association, then the network key after 130 ms */
static void steer(void)
{
	zassert_ok(zzb_bdb_steering(child));
	k_sleep(K_SECONDS(2));
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
	call->err = zzb_settings_load(child);
	if (call->err == 0) {
		call->err = zzb_fc_start(&child->nwk.fc);
	}

	if (call->err == 0) {
		call->err = zzb_fc_start(&child->aps.fc);
	}

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

/* BDB 8.2 with the trust center link key exchange of BDB 10.2.5 */
ZTEST(zzb_bdb_integration, test_steering)
{
	const struct zzb_aps_key_pair *pair;

	steer();
	check_event(ZZB_BDB_EVENT_STEERING, ZZB_BDB_SUCCESS);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
	zassert_equal(parent_log.device_annces, 1);
	zassert_equal(parent_log.node_desc_reqs, 1);

	pair = zzb_aps_key_pair_find(child, PARENT_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, tc_link_key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_VERIFIED);

	/* BDB 8.2 step 14 */
	zassert_equal(parent_log.permit_joining_reqs, 1);
	zassert_equal(parent_log.permit_duration, 180);
	zassert_equal(parent_log.tc_significance, 1);
	zassert_equal(parent_log.zdo_dst, 0xfffc);
}

/* BDB 10.2.5 step 5: Z-Stack Home 1.2 reports revision 0 */
ZTEST(zzb_bdb_integration, test_steering_with_an_r20_trust_center)
{
	const struct zzb_aps_key_pair *pair;

	parent_set_stack_revision(0);
	steer();
	check_event(ZZB_BDB_EVENT_STEERING, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.node_desc_reqs, 1);
	zassert_equal(parent_log.aps_cmd.id, 0);
	zassert_equal(parent_log.permit_joining_reqs, 1);

	pair = zzb_aps_key_pair_find(child, PARENT_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, zzb_aps_default_tc_link_key, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_PROVISIONAL);
}

/* BDB 7.1: a secure rejoin on the channel of the network, then Device_annce */
ZTEST(zzb_bdb_integration, test_reboot_rejoins)
{
	steer();
	reboot();
	zassert_ok(zzb_bdb_start(child));
	k_sleep(K_SECONDS(2));

	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.rejoin_reqs, 1);
	zassert_true(parent_log.rejoin_hdr.flags & ZZB_NWK_SECURITY);
	zassert_equal(parent_log.device_annces, 2);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
}

/* BDB 9.4: the response, the leave command, then the reset */
ZTEST(zzb_bdb_integration, test_mgmt_leave_resets_the_node)
{
	steer();
	events.count = 0;
	parent_send_mgmt_leave(false);
	k_sleep(K_MSEC(100));

	check_event(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.mgmt_leave_rsps, 1);
	zassert_equal(parent_log.leaves, 1);
	zassert_equal(parent_log.cmd.leave_options, 0);
	zassert_is_null(zzb_aps_key_pair_find(child, PARENT_EUI64));

	/* The NWK records are gone */
	reboot();
	zassert_ok(zzb_bdb_start(child));
	check_event(ZZB_BDB_EVENT_INIT, ZZB_BDB_NOT_ON_A_NETWORK);
}

/*
 * The poll keepalive fails three times, 3.33 s apart; the secure rejoin on the old channel finds
 * no beacon, the one on all channels finds the parent
 */
ZTEST(zzb_bdb_integration, test_parent_moves_to_another_channel)
{
	steer();
	events.count = 0;
	parent_set_channel(20);
	k_sleep(K_SECONDS(16));

	check_event(ZZB_BDB_EVENT_REJOIN, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.rejoin_reqs, 1);
	zassert_equal(parent_log.device_annces, 2);
	zassert_equal(child->nwk.nib.channel, 20);
}

ZTEST_SUITE(zzb_bdb_integration, NULL, setup, before, NULL, NULL);
