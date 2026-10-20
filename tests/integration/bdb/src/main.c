/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/time.h>
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

/* The install code of BDB 10.1.1 and its key, which is also in ci/check-frame-log.sh */
static const uint8_t install_code[] = {
	0x83, 0xfe, 0xd3, 0x40, 0x7a, 0x93, 0x97, 0x23, 0xa5,
	0xc6, 0x39, 0xb2, 0x69, 0x16, 0xd5, 0x05, 0xc3, 0xb5,
};

static const uint8_t install_code_key[ZZB_SEC_KEY_LEN] = {
	0x66, 0xb6, 0x90, 0x09, 0x81, 0xe1, 0xee, 0x3c,
	0xa4, 0x20, 0x6b, 0x6b, 0x86, 0x1c, 0x02, 0xbb,
};

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

/* The calls of zcl_cb */
static struct {
	int count;
	uint8_t ep;
	uint16_t cluster;
	uint16_t attr;
} changes;

static void record_change(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	changes.count++;
	changes.ep = ep;
	changes.cluster = cluster;
	changes.attr = attr;
}

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "bdb",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_level level = {
	.start_up_default = 0xff,
};

static struct zzb_zcl_ias_zone zone = {
	.zone_type = 0x0015,
};

/*
 * Basic, Identify, Groups, On/Off and Level Control servers of a Dimmable Light, an IAS Zone server
 * of a contact switch and a Time client, since the parent sends to endpoint 1 only. The Time client
 * sends its first request 60 s after the reset of the node, when the other tests have ended.
 */
static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_LEVEL_SERVER(&level),
	ZZB_ZCL_IAS_ZONE_SERVER(&zone),
	ZZB_ZCL_TIME_CLIENT(),
};

/* An On/Off Switch, the initiator of finding & binding */
static const struct zzb_zcl_cluster switch_clusters[] = {
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0101,
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0000,
		.clusters = switch_clusters,
		.cluster_count = ARRAY_SIZE(switch_clusters),
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
		.zcl_cb = record_change,
		.install_code = install_code,
		.install_code_len = sizeof(install_code),
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
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_GLOBAL);
	zassert_equal(pair->attr, ZZB_APS_KEY_PROVISIONAL);
}

/*
 * BDB 10.1: the network key under the key of the install code. Without the key exchange of an R20
 * trust center the entry keeps that key.
 */
ZTEST(zzb_bdb_integration, test_steering_with_an_install_code)
{
	const struct zzb_aps_key_pair *pair;

	parent_set_link_key(install_code_key);
	parent_set_stack_revision(0);
	steer();
	check_event(ZZB_BDB_EVENT_STEERING, ZZB_BDB_SUCCESS);

	pair = zzb_aps_key_pair_find(child, PARENT_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, install_code_key, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_UNIQUE);
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

/* The interview read of zigpy, then Toggle from the coordinator */
ZTEST(zzb_bdb_integration, test_zcl)
{
	static const uint8_t read[] = {0x00, 0x10, 0x00, 0x04, 0x00, 0x05, 0x00};
	static const uint8_t read_rsp[] = {0x18, 0x10, 0x01, 0x04, 0x00, 0x00, 0x42, 0x03, 'z', 'z',
					   'b',  0x05, 0x00, 0x00, 0x42, 0x03, 'b',  'd',  'b'};
	static const uint8_t toggle[] = {0x01, 0x11, 0x02};
	static const uint8_t default_rsp[] = {0x18, 0x11, 0x0b, 0x02, 0x00};

	steer();
	changes.count = 0;
	parent_send_zcl(ZZB_ZCL_BASIC, read, sizeof(read));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 1);
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_BASIC);
	zassert_equal(parent_log.zcl_src_ep, 1);
	zassert_equal(parent_log.zcl_dst_ep, 1);
	zassert_equal(parent_log.zcl_len, sizeof(read_rsp));
	zassert_mem_equal(parent_log.zcl, read_rsp, sizeof(read_rsp));

	parent_send_zcl(ZZB_ZCL_ON_OFF, toggle, sizeof(toggle));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 2);
	zassert_equal(parent_log.zcl_len, sizeof(default_rsp));
	zassert_mem_equal(parent_log.zcl, default_rsp, sizeof(default_rsp));
	zassert_true(on_off.on_off);
	zassert_equal(changes.count, 1);
	zassert_equal(changes.ep, 1);
	zassert_equal(changes.cluster, ZZB_ZCL_ON_OFF);
	zassert_equal(changes.attr, ZZB_ZCL_ON_OFF_ON_OFF);
}

/*
 * ZHA configures reporting: Bind_req of On/Off to endpoint 1 of the coordinator, then Configure
 * Reporting of OnOff with min 0 and max 900. A Toggle sends a report through the binding.
 */
ZTEST(zzb_bdb_integration, test_reporting)
{
	static const uint8_t configure[] = {0x00, 0x20, 0x06, 0x00, 0x00, 0x00,
					    0x10, 0x00, 0x00, 0x84, 0x03};
	static const uint8_t configure_rsp[] = {0x18, 0x20, 0x07, 0x00};
	static const uint8_t toggle[] = {0x01, 0x21, 0x02};
	static const uint8_t report[] = {0x0a, 0x00, 0x00, 0x10, 0x01};
	uint8_t bind[22] = {0x22};

	steer();
	sys_put_le64(child->eui64, bind + 1);
	bind[9] = 1;
	sys_put_le16(ZZB_ZCL_ON_OFF, bind + 10);
	bind[12] = ZZB_APS_ADDR_EXT;
	sys_put_le64(PARENT_EUI64, bind + 13);
	bind[21] = 1;
	parent_send_zdo(ZZB_ZDP_BIND_REQ, bind, sizeof(bind));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zdo_cluster, ZZB_ZDP_BIND_REQ | 0x8000);

	parent_send_zcl(ZZB_ZCL_ON_OFF, configure, sizeof(configure));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_len, sizeof(configure_rsp));
	zassert_mem_equal(parent_log.zcl, configure_rsp, sizeof(configure_rsp));

	/* The Default Response, then the report with a TSN of the node */
	parent_send_zcl(ZZB_ZCL_ON_OFF, toggle, sizeof(toggle));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 3);
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_ON_OFF);
	zassert_equal(parent_log.zcl_dst_ep, 1);
	zassert_true(parent_log.aps_hdr.flags & ZZB_APS_ACK_REQ);
	zassert_equal(parent_log.zcl_len, 2 + sizeof(report));
	zassert_equal(parent_log.zcl[0], 0x18);
	zassert_mem_equal(parent_log.zcl + 2, report, sizeof(report));
}

/*
 * ZHA binds Level Control and configures reporting of CurrentLevel with min 1, max 900 and change
 * 1. It turns the light on with a brightness by Move to Level (with On/Off) without the Options
 * fields, and off with a transition by the same command to level 0.
 */
ZTEST(zzb_bdb_integration, test_level)
{
	static const uint8_t configure[] = {0x00, 0x50, 0x06, 0x00, 0x00, 0x00,
					    0x20, 0x01, 0x00, 0x84, 0x03, 0x01};
	static const uint8_t configure_rsp[] = {0x18, 0x50, 0x07, 0x00};
	static const uint8_t on_128[] = {0x01, 0x51, 0x04, 0x80, 0x0a, 0x00};
	static const uint8_t off[] = {0x01, 0x52, 0x04, 0x00, 0x05, 0x00};
	static const uint8_t report[] = {0x0a, 0x00, 0x00, 0x20, 0x80};
	uint8_t bind[22] = {0x23};

	steer();
	sys_put_le64(child->eui64, bind + 1);
	bind[9] = 1;
	sys_put_le16(ZZB_ZCL_LEVEL, bind + 10);
	bind[12] = ZZB_APS_ADDR_EXT;
	sys_put_le64(PARENT_EUI64, bind + 13);
	bind[21] = 1;
	parent_send_zdo(ZZB_ZDP_BIND_REQ, bind, sizeof(bind));
	k_sleep(K_MSEC(100));
	parent_send_zcl(ZZB_ZCL_LEVEL, configure, sizeof(configure));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_len, sizeof(configure_rsp));
	zassert_mem_equal(parent_log.zcl, configure_rsp, sizeof(configure_rsp));

	/* 1 s to level 128; the last report after the minimum interval has the end value */
	parent_send_zcl(ZZB_ZCL_LEVEL, on_128, sizeof(on_128));
	k_sleep(K_MSEC(2500));
	zassert_true(on_off.on_off);
	zassert_equal(level.current_level, 0x80);
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_LEVEL);
	zassert_equal(parent_log.zcl_len, 2 + sizeof(report));
	zassert_equal(parent_log.zcl[0], 0x18);
	zassert_mem_equal(parent_log.zcl + 2, report, sizeof(report));

	/* Level 0 is the minimum, and OnOff goes FALSE at its end */
	parent_send_zcl(ZZB_ZCL_LEVEL, off, sizeof(off));
	k_sleep(K_MSEC(300));
	zassert_true(on_off.on_off);
	k_sleep(K_MSEC(500));
	zassert_false(on_off.on_off);
	zassert_equal(level.current_level, 1);
}

/*
 * ZHA adds the light to a group with a named Add Group, then switches the group on. A frame to
 * another group is dropped.
 */
ZTEST(zzb_bdb_integration, test_group)
{
	static const uint8_t add_group[] = {0x01, 0x30, 0x00, 0x42, 0x00, 0x03, 'z', 'h', 'a'};
	static const uint8_t add_group_rsp[] = {0x19, 0x30, 0x00, 0x00, 0x42, 0x00};
	static const uint8_t on[] = {0x01, 0x31, 0x01};
	static const uint8_t off[] = {0x01, 0x32, 0x00};

	steer();
	parent_send_zcl(ZZB_ZCL_GROUPS, add_group, sizeof(add_group));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_len, sizeof(add_group_rsp));
	zassert_mem_equal(parent_log.zcl, add_group_rsp, sizeof(add_group_rsp));

	/* No Default Response to a group */
	parent_send_group_zcl(0x0042, ZZB_ZCL_ON_OFF, on, sizeof(on));
	k_sleep(K_MSEC(100));
	zassert_true(on_off.on_off);
	zassert_equal(parent_log.zcl_frames, 1);

	parent_send_group_zcl(0x0043, ZZB_ZCL_ON_OFF, off, sizeof(off));
	k_sleep(K_MSEC(100));
	zassert_true(on_off.on_off);
}

/*
 * ZHA enrolls an IAS Zone: it writes its IEEE address to IAS_CIE_Address from endpoint 1, after
 * which the zone sends Zone Enroll Request to that address, the trust center, then ZHA sends Zone
 * Enroll Response. A change of ZoneStatus sends Zone Status Change Notification with the zone ID.
 */
ZTEST(zzb_bdb_integration, test_ias_zone)
{
	static const uint8_t enroll_req[] = {0x01, 0x15, 0x00, 0x00, 0x00};
	static const uint8_t enroll_rsp[] = {0x01, 0x61, 0x00, 0x00, 0x05};
	static const uint8_t notification[] = {0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x00};
	uint8_t write[14] = {0x00, 0x60, 0x02, 0x10, 0x00, 0xf0};
	uint16_t status = ZZB_ZCL_IAS_ZONE_STATUS_ALARM1;

	steer();
	sys_put_le64(PARENT_EUI64, write + 6);
	parent_send_zcl(ZZB_ZCL_IAS_ZONE, write, sizeof(write));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 2);
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_IAS_ZONE);
	zassert_equal(parent_log.zcl_dst_ep, 1);
	zassert_equal(parent_log.zcl_len, 2 + sizeof(enroll_req));
	zassert_equal(parent_log.zcl[0], 0x19);
	zassert_mem_equal(parent_log.zcl + 2, enroll_req, sizeof(enroll_req));

	parent_send_zcl(ZZB_ZCL_IAS_ZONE, enroll_rsp, sizeof(enroll_rsp));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 3);
	zassert_equal(zone.zone_state, 1);
	zassert_equal(zone.zone_id, 5);

	zassert_ok(zzb_zcl_attr_set(child, 1, ZZB_ZCL_IAS_ZONE, ZZB_ZCL_IAS_ZONE_STATUS, &status));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_frames, 4);
	zassert_equal(parent_log.zcl_len, 2 + sizeof(notification));
	zassert_equal(parent_log.zcl[0], 0x19);
	zassert_mem_equal(parent_log.zcl + 2, notification, sizeof(notification));
}

/*
 * Without a binding the Time client reads the time from endpoint 1 of the coordinator. zigpy
 * answers with Time, TimeStatus 0x05 (Master, MasterZoneDst), TimeZone and LocalTime.
 */
ZTEST(zzb_bdb_integration, test_time)
{
	static const uint8_t read[] = {0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00};
	uint8_t rsp[] = {0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00, 0x30,
			 0x01, 0x00, 0x00, 0x18, 0x05, 0x02, 0x00, 0x00, 0x2b, 0x10, 0x0e,
			 0x00, 0x00, 0x07, 0x00, 0x00, 0x23, 0x20, 0x1c, 0x00, 0x30};
	uint32_t utc;
	uint32_t local;

	steer();
	zassert_equal(zzb_zcl_time_get(child, &utc, &local), -ENODATA);
	k_sleep(K_SECONDS(60));
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_TIME);
	zassert_equal(parent_log.zcl_src_ep, 1);
	zassert_equal(parent_log.zcl_dst_ep, 1);
	zassert_equal(parent_log.zcl_len, 3 + sizeof(read));
	zassert_equal(parent_log.zcl[0], 0x10);
	zassert_equal(parent_log.zcl[2], 0x00);
	zassert_mem_equal(parent_log.zcl + 3, read, sizeof(read));

	rsp[1] = parent_log.zcl[1];
	parent_send_zcl(ZZB_ZCL_TIME, rsp, sizeof(rsp));
	k_sleep(K_MSEC(100));
	zassert_ok(zzb_zcl_time_get(child, &utc, &local));
	zassert_equal(utc, 0x30000000);
	zassert_equal(local, 0x30000000 + 7200);
}

/* BDB 8.4: the coordinator finds the identifying light, then ends the identification */
ZTEST(zzb_bdb_integration, test_finding_binding_target)
{
	static const uint8_t query[] = {0x01, 0x40, 0x01};
	static const uint8_t query_rsp[] = {0x19, 0x40, 0x00, 0xb4, 0x00};
	static const uint8_t identify_off[] = {0x01, 0x41, 0x00, 0x00, 0x00};

	steer();
	events.count = 0;
	zassert_ok(zzb_bdb_finding_binding_target(child, 1));
	parent_send_zcl(ZZB_ZCL_IDENTIFY, query, sizeof(query));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_len, sizeof(query_rsp));
	zassert_mem_equal(parent_log.zcl, query_rsp, sizeof(query_rsp));
	zassert_equal(events.count, 0);

	parent_send_zcl(ZZB_ZCL_IDENTIFY, identify_off, sizeof(identify_off));
	k_sleep(K_MSEC(100));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_SUCCESS);
}

/*
 * BDB 8.5: the switch finds endpoint 1 of the coordinator and binds its On/Off client to the
 * On/Off server there
 */
ZTEST(zzb_bdb_integration, test_finding_binding_initiator)
{
	const struct zzb_aps_binding *bindings = child->aps.aib.bindings;

	steer();
	events.count = 0;
	/* The switch has no binding yet */
	zassert_equal(zzb_zcl_send_bound(child, 2, ZZB_ZCL_ON_OFF, 0x02, NULL, 0), -ENOENT);
	zassert_ok(zzb_bdb_finding_binding_initiator(child, 2, 0xffff));
	k_sleep(K_MSEC(5500));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.identify_queries, 1);
	zassert_equal(parent_log.simple_desc_reqs, 1);
	zassert_equal(parent_log.ieee_addr_reqs, 1);

	zassert_true(bindings[0].in_use);
	zassert_equal(bindings[0].src_ep, 2);
	zassert_equal(bindings[0].cluster, ZZB_ZCL_ON_OFF);
	zassert_equal(bindings[0].dst_mode, ZZB_APS_ADDR_EXT);
	zassert_equal(bindings[0].dst_ext, PARENT_EUI64);
	zassert_equal(bindings[0].dst_ep, 1);
	zassert_false(bindings[1].in_use);

	/* The switch toggles the light it has found */
	zassert_ok(zzb_zcl_send_bound(child, 2, ZZB_ZCL_ON_OFF, 0x02, NULL, 0));
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_ON_OFF);
	zassert_equal(parent_log.zcl_src_ep, 2);
	zassert_equal(parent_log.zcl_dst_ep, 1);
	zassert_equal(parent_log.zcl_len, 3);
	zassert_equal(parent_log.zcl[0], 0x11);
	zassert_equal(parent_log.zcl[2], 0x02);
}

/* BDB 8.5 step 10: a group binding, then Add Group If Identifying to all devices */
ZTEST(zzb_bdb_integration, test_finding_binding_initiator_with_a_group)
{
	static const uint8_t add_group[] = {0x05, 0x42, 0x00, 0x00};

	steer();
	events.count = 0;
	zassert_ok(zzb_bdb_finding_binding_initiator(child, 2, 0x0042));
	k_sleep(K_MSEC(5500));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
	zassert_equal(parent_log.ieee_addr_reqs, 0);
	zassert_equal(child->aps.aib.bindings[0].dst_mode, ZZB_APS_ADDR_GROUP);
	zassert_equal(child->aps.aib.bindings[0].dst_addr, 0x0042);

	zassert_equal(parent_log.zcl_cluster, ZZB_ZCL_GROUPS);
	zassert_equal(parent_log.zcl_dst_ep, 0xff);
	zassert_equal(parent_log.zcl_len, 2 + sizeof(add_group));
	zassert_mem_equal(parent_log.zcl + 2, add_group, sizeof(add_group));
}

ZTEST_SUITE(zzb_bdb_integration, NULL, setup, before, NULL, NULL);
