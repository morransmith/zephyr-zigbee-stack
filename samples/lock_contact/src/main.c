/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/door_lock.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/time.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(lock_contact, LOG_LEVEL_INF);

#define LOCK_EP    1
#define CONTACT_EP 2

/* The shortest PIN code of the users, in octets */
#define PIN_MIN 4

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "lock_contact",
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify lock_identify;
static struct zzb_zcl_identify contact_identify;
static struct zzb_zcl_alarms alarms;

/* A dead bolt in a closed door */
static struct zzb_zcl_door_lock lock = {
	.lock_state = ZZB_ZCL_DOOR_LOCK_LOCKED,
	.lock_type = ZZB_ZCL_DOOR_LOCK_TYPE_DEAD_BOLT,
	.actuator_enabled = true,
	.door_state = ZZB_ZCL_DOOR_LOCK_DOOR_STATE_CLOSED,
	.min_pin_code_length = PIN_MIN,
	.max_pin_code_length = ZZB_ZCL_DOOR_LOCK_PIN_MAX,
};

static struct zzb_zcl_ias_zone contact = {
	.zone_type = ZZB_ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH,
};

static const struct zzb_zcl_cluster lock_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&lock_identify),
	ZZB_ZCL_ALARMS_SERVER(&alarms),
	ZZB_ZCL_DOOR_LOCK_SERVER(&lock),
	ZZB_ZCL_TIME_CLIENT(),
};

static const struct zzb_zcl_cluster contact_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&contact_identify),
	ZZB_ZCL_IAS_ZONE_SERVER(&contact),
};

/* A Door Lock and an IAS Zone, the contact of the door */
static const struct zzb_endpoint endpoints[] = {
	{
		.id = LOCK_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_DOOR_LOCK,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_DOOR_LOCK),
		.clusters = lock_clusters,
		.cluster_count = ARRAY_SIZE(lock_clusters),
	},
	{
		.id = CONTACT_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_IAS_ZONE,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_IAS_ZONE),
		.clusters = contact_clusters,
		.cluster_count = ARRAY_SIZE(contact_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The bolt, the door and AutoRelockTime for the button, which runs outside the stack thread */
static atomic_t locked;
static atomic_t door_open;
static atomic_t auto_relock;

static int bolt_set(struct zzb_node *node, bool on)
{
	uint8_t state = on ? ZZB_ZCL_DOOR_LOCK_LOCKED : ZZB_ZCL_DOOR_LOCK_UNLOCKED;
	int err = zzb_zcl_attr_set(node, LOCK_EP, ZZB_ZCL_DOOR_LOCK, ZZB_ZCL_DOOR_LOCK_LOCK_STATE,
				   &state);

	if (err) {
		LOG_ERR("LockState %d", err);
		return err;
	}

	atomic_set(&locked, on);
	sample_led(on);
	LOG_INF("door %s", on ? "locked" : "unlocked");
	return 0;
}

/*
 * Lock Door and Unlock Door after the checks of the stack, and the relock after AutoRelockTime: the
 * bolt does not lock an open door
 */
static uint8_t zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	bool on = cmd->id == ZZB_ZCL_DOOR_LOCK_LOCK_DOOR;

	if (on && atomic_get(&door_open)) {
		LOG_INF("the door is open");
		return ZZB_ZCL_FAILURE;
	}

	return bolt_set(node, on) ? ZZB_ZCL_FAILURE : ZZB_ZCL_SUCCESS;
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(lock_identify.identify_time > 0 ||
				contact_identify.identify_time > 0);
		return;
	}

	if (cluster == ZZB_ZCL_DOOR_LOCK && attr == ZZB_ZCL_DOOR_LOCK_AUTO_RELOCK_TIME) {
		atomic_set(&auto_relock, lock.auto_relock_time);
		LOG_INF("AutoRelockTime %u s", lock.auto_relock_time);
	}
}

/* The contact sets Alarm1 of the zone and DoorState of the lock */
static int door_set(struct zzb_node *node, bool open)
{
	uint16_t status = open ? ZZB_ZCL_IAS_ZONE_STATUS_ALARM1 : 0;
	uint8_t state =
		open ? ZZB_ZCL_DOOR_LOCK_DOOR_STATE_OPEN : ZZB_ZCL_DOOR_LOCK_DOOR_STATE_CLOSED;
	int err = zzb_zcl_attr_set(node, CONTACT_EP, ZZB_ZCL_IAS_ZONE, ZZB_ZCL_IAS_ZONE_STATUS,
				   &status);

	if (err) {
		LOG_ERR("ZoneStatus %d", err);
		return err;
	}

	err = zzb_zcl_attr_set(node, LOCK_EP, ZZB_ZCL_DOOR_LOCK, ZZB_ZCL_DOOR_LOCK_DOOR_STATE,
			       &state);
	if (err) {
		LOG_ERR("DoorState %d", err);
		return err;
	}

	atomic_set(&door_open, open);
	LOG_INF("door %s", open ? "open" : "closed");
	return 0;
}

/* The door opens and closes; a lock with AutoRelockTime locks the door that closes */
void sample_button(struct zzb_node *node)
{
	bool open = !atomic_get(&door_open);

	if (door_set(node, open) || open) {
		return;
	}

	if (atomic_get(&auto_relock) && !atomic_get(&locked)) {
		bolt_set(node, true);
	}
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, LOCK_EP));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_cmd_cb = zcl_cmd,
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

	/* The bolt starts locked; the record has set AutoRelockTime without zcl_cb */
	atomic_set(&locked, true);
	atomic_set(&auto_relock, lock.auto_relock_time);
	sample_led(true);
	return zzb_bdb_start(node);
}
