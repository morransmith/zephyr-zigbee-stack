/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/ballast.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/device_temp.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(ballast, LOG_LEVEL_INF);

#define BALLAST_EP 1

#if defined(CONFIG_SAMPLE_ON_OFF_BALLAST)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_BALLAST
#elif defined(CONFIG_SAMPLE_DIMMABLE_BALLAST)
#define DEVICE_ID ZZB_DEVICE_DIMMABLE_BALLAST
#endif

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

/* 230.0 V, 50 Hz */
static struct zzb_zcl_power_config power_config = {
	.mains_voltage = 2300,
	.mains_frequency = 100,
	.battery_voltage = ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_UNKNOWN,
	.battery_percentage_remaining = ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_UNKNOWN,
};

/* 25 degC */
static struct zzb_zcl_device_temp device_temp = {
	.current_temperature = 25,
};

static struct zzb_zcl_identify identify;
static struct zzb_zcl_scenes scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

/* The On/Off Ballast has Level Control for the commands with On/Off to its group (DTL 29.3.1) */
static struct zzb_zcl_level level = {
	.start_up_default = ZZB_ZCL_LEVEL_START_UP_PREVIOUS,
};

static struct zzb_zcl_alarms alarms;

static struct zzb_zcl_ballast ballast = {
	.physical_min_level = ZZB_ZCL_BALLAST_LEVEL_MIN,
	.physical_max_level = ZZB_ZCL_BALLAST_LEVEL_MAX,
	.lamp_quantity = 1,
};

static struct zzb_zcl_illuminance_level illuminance_level;

static const struct zzb_zcl_cluster ballast_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_DEVICE_TEMP_SERVER(&device_temp),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_LEVEL_SERVER(&level),
	ZZB_ZCL_ALARMS_SERVER(&alarms),
	ZZB_ZCL_BALLAST_SERVER(&ballast),
	ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&illuminance_level),
	ZZB_ZCL_ILLUMINANCE_CLIENT(),
	ZZB_ZCL_OCCUPANCY_CLIENT(),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = BALLAST_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = ballast_clusters,
		.cluster_count = ARRAY_SIZE(ballast_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

static struct zzb_node *ballast_node;

/* OnOff and LampBurnHours for the button and the timer, which run outside the stack thread */
static atomic_t lamp_on;
static atomic_t burn_hours;

static void lamp_set(bool on)
{
	atomic_set(&lamp_on, on);
	sample_led(on);
	LOG_INF("lamp %s", on ? "on" : "off");
}

static void burn_hour(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(burn_hour_work, burn_hour);

/* LampBurnHours counts the hours at whose end the lamp is on */
static void burn_hour(struct k_work *work)
{
	uint32_t hours = atomic_get(&burn_hours);
	int err;

	k_work_schedule(&burn_hour_work, K_HOURS(1));
	if (!atomic_get(&lamp_on) || hours == ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_UNKNOWN) {
		return;
	}

	hours++;
	err = zzb_zcl_attr_set(ballast_node, BALLAST_EP, ZZB_ZCL_BALLAST,
			       ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, &hours);
	if (err) {
		LOG_ERR("LampBurnHours %d", err);
		return;
	}

	atomic_set(&burn_hours, hours);
	LOG_INF("lamp burn hours %u", hours);
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			lamp_set(on_off.on_off);
		}
		break;
	case ZZB_ZCL_LEVEL:
		if (attr == ZZB_ZCL_LEVEL_CURRENT_LEVEL) {
			LOG_INF("level %u", level.current_level);
		}
		break;
	case ZZB_ZCL_BALLAST:
		if (attr == ZZB_ZCL_BALLAST_LAMP_BURN_HOURS) {
			atomic_set(&burn_hours, ballast.lamp_burn_hours);
		}
		break;
	default:
		break;
	}
}

void sample_button(struct zzb_node *node)
{
	bool on = !atomic_get(&lamp_on);
	int err = zzb_zcl_attr_set(node, BALLAST_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	lamp_set(on);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, BALLAST_EP));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
	};
	int err = sample_init(&cfg, &ballast_node);

	if (err) {
		return err;
	}

	/* StartUpOnOff and the records have set OnOff and LampBurnHours without zcl_cb */
	lamp_set(on_off.on_off);
	atomic_set(&burn_hours, ballast.lamp_burn_hours);
	k_work_schedule(&burn_hour_work, K_HOURS(1));
	return zzb_bdb_start(ballast_node);
}
