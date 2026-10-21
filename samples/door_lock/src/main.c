/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/door_lock.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/time.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(door_lock, LOG_LEVEL_INF);

#define LOCK_EP 1

#if defined(CONFIG_SAMPLE_DOOR_LOCK)
#define DEVICE_ID ZZB_DEVICE_DOOR_LOCK
#elif defined(CONFIG_SAMPLE_DOOR_LOCK_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_DOOR_LOCK_CONTROLLER
#endif

/* The shortest PIN code of the users, in octets */
#define PIN_MIN 4

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

#if defined(CONFIG_SAMPLE_SLEEPY)
/* Two AA cells of 1.5 V, at 3.0 V and 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
#endif

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_DOOR_LOCK)
static struct zzb_zcl_alarms alarms;

/* A dead bolt without a door sensor */
static struct zzb_zcl_door_lock lock = {
	.lock_state = ZZB_ZCL_DOOR_LOCK_LOCKED,
	.lock_type = ZZB_ZCL_DOOR_LOCK_TYPE_DEAD_BOLT,
	.actuator_enabled = true,
	.door_state = ZZB_ZCL_DOOR_LOCK_DOOR_STATE_UNDEFINED,
	.min_pin_code_length = PIN_MIN,
	.max_pin_code_length = ZZB_ZCL_DOOR_LOCK_PIN_MAX,
};
#endif

static const struct zzb_zcl_cluster door_lock[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
#if defined(CONFIG_SAMPLE_SLEEPY)
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
#endif
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
#if defined(CONFIG_SAMPLE_DOOR_LOCK)
	ZZB_ZCL_ALARMS_SERVER(&alarms),
	ZZB_ZCL_DOOR_LOCK_SERVER(&lock),
	ZZB_ZCL_TIME_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_DOOR_LOCK_CONTROLLER)
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_DOOR_LOCK_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = LOCK_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = door_lock,
		.cluster_count = ARRAY_SIZE(door_lock),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

#if defined(CONFIG_SAMPLE_DOOR_LOCK)
/* The bolt for the button; led0 is on while it is locked */
static atomic_t locked;

static void bolt_show(bool on)
{
	atomic_set(&locked, on);
	sample_led(on);
	LOG_INF("door %s", on ? "locked" : "unlocked");
}

static int bolt_set(struct zzb_node *node, bool on)
{
	uint8_t state = on ? ZZB_ZCL_DOOR_LOCK_LOCKED : ZZB_ZCL_DOOR_LOCK_UNLOCKED;
	int err = zzb_zcl_attr_set(node, LOCK_EP, ZZB_ZCL_DOOR_LOCK, ZZB_ZCL_DOOR_LOCK_LOCK_STATE,
				   &state);

	if (err) {
		LOG_ERR("LockState %d", err);
		return err;
	}

	bolt_show(on);
	return 0;
}

/* Lock Door and Unlock Door after the checks of the stack, which answers with the status */
static uint8_t zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	bool on = cmd->id == ZZB_ZCL_DOOR_LOCK_LOCK_DOOR;

	return bolt_set(node, on) ? ZZB_ZCL_FAILURE : ZZB_ZCL_SUCCESS;
}
#endif

#if defined(CONFIG_SAMPLE_DOOR_LOCK_CONTROLLER)
/* Lock Door and Unlock Door in turn, without a code */
static void controller_send(struct zzb_node *node)
{
	static bool unlock;
	int err = unlock ? zzb_zcl_door_lock_unlock_door(node, LOCK_EP, NULL, 0)
			 : zzb_zcl_door_lock_lock_door(node, LOCK_EP, NULL, 0);

	if (err) {
		LOG_ERR("%s %d", unlock ? "Unlock Door" : "Lock Door", err);
		return;
	}

	LOG_INF("%s sent", unlock ? "Unlock Door" : "Lock Door");
	unlock = !unlock;
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(identify.identify_time > 0);
	}
}

void sample_button(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_DOOR_LOCK)
	bolt_set(node, !atomic_get(&locked));
#else
	controller_send(node);
#endif
}

void sample_finding_binding(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_DOOR_LOCK)
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, LOCK_EP));
#else
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, LOCK_EP, ZZB_BDB_FB_UNICAST));
#endif
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
#if defined(CONFIG_SAMPLE_DOOR_LOCK)
		.zcl_cmd_cb = zcl_cmd,
#endif
		.sleepy = IS_ENABLED(CONFIG_SAMPLE_SLEEPY),
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

#if defined(CONFIG_SAMPLE_DOOR_LOCK)
	bolt_show(lock.lock_state == ZZB_ZCL_DOOR_LOCK_LOCKED);
#endif
	return zzb_bdb_start(node);
}
