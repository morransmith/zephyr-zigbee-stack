/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "common/node.h"
#include "fake_radio.h"
#include "nwk/nwk.h"
#include "parent.h"

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
static const uint16_t light_in[] = {0x0000, 0x0003, 0x0006};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.in_clusters = light_in,
		.in_cluster_count = ARRAY_SIZE(light_in),
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

ZTEST_SUITE(zzb_bdb_integration, NULL, setup, before, NULL, NULL);
