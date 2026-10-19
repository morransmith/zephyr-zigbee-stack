/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/ballast.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/color.h>
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

/*
 * An Extended Color Light, a Color Dimmer Switch, a Dimmable Ballast and an Occupancy Sensor
 * (Home Automation profile)
 */
#if defined(CONFIG_ZZB_ZCL)
static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "build",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;
static struct zzb_zcl_identify switch_identify;
static struct zzb_zcl_identify ballast_identify;
static struct zzb_zcl_identify sensor_identify;

static struct zzb_zcl_scenes scenes;
static struct zzb_zcl_scenes ballast_scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_on_off ballast_on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_level level = {
	.start_up_default = 0xff,
};

static struct zzb_zcl_level ballast_level = {
	.start_up_default = 0xff,
};

static struct zzb_zcl_color color = {
	.color_capabilities = ZZB_ZCL_COLOR_CAP_XY | ZZB_ZCL_COLOR_CAP_TEMPERATURE,
	.color_temp_physical_min = 153,
	.color_temp_physical_max = 500,
	.couple_color_temp_min = 153,
	.start_up_default = 0xffff,
};

static struct zzb_zcl_power_config power_config = {
	.mains_voltage = 2300,
	.mains_frequency = 100,
	.battery_voltage = 0xff,
	.battery_percentage_remaining = 0xff,
};

static struct zzb_zcl_device_temp device_temp = {
	.current_temperature = 25,
};

static struct zzb_zcl_ballast ballast = {
	.physical_min_level = 1,
	.physical_max_level = 254,
	.lamp_quantity = 1,
};

static struct zzb_zcl_alarms alarms;
static struct zzb_zcl_illuminance_level illuminance_level;
static struct zzb_zcl_illuminance illuminance;

static struct zzb_zcl_occupancy occupancy = {
	.occupancy_sensor_type_bitmap = 0x01,
};

static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_LEVEL_SERVER(&level),
	ZZB_ZCL_COLOR_SERVER(&color),
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&switch_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
	ZZB_ZCL_LEVEL_CLIENT(),
	ZZB_ZCL_COLOR_CLIENT(),
	ZZB_ZCL_SCENES_CLIENT(),
	ZZB_ZCL_GROUPS_CLIENT(),
};

static const struct zzb_zcl_cluster ballast_clusters[] = {
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_DEVICE_TEMP_SERVER(&device_temp),
	ZZB_ZCL_IDENTIFY_SERVER(&ballast_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&ballast_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&ballast_on_off),
	ZZB_ZCL_LEVEL_SERVER(&ballast_level),
	ZZB_ZCL_ALARMS_SERVER(&alarms),
	ZZB_ZCL_BALLAST_SERVER(&ballast),
	ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&illuminance_level),
	ZZB_ZCL_ILLUMINANCE_CLIENT(),
	ZZB_ZCL_OCCUPANCY_CLIENT(),
};

static const struct zzb_zcl_cluster sensor[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&sensor_identify),
	ZZB_ZCL_ILLUMINANCE_SERVER(&illuminance),
	ZZB_ZCL_OCCUPANCY_SERVER(&occupancy),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};
#else
/* native_sim builds no ZCL, so the definitions of the stack clusters do not exist */
static const struct zzb_zcl_cluster light[] = {
	{.id = ZZB_ZCL_BASIC, .server = true},
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_LEVEL, .server = true},
	{.id = ZZB_ZCL_COLOR, .server = true},
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_ON_OFF},
	{.id = ZZB_ZCL_LEVEL},
	{.id = ZZB_ZCL_COLOR},
	{.id = ZZB_ZCL_SCENES},
	{.id = ZZB_ZCL_GROUPS},
};

static const struct zzb_zcl_cluster ballast_clusters[] = {
	{.id = ZZB_ZCL_POWER_CONFIG, .server = true},
	{.id = ZZB_ZCL_DEVICE_TEMP, .server = true},
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_LEVEL, .server = true},
	{.id = ZZB_ZCL_ALARMS, .server = true},
	{.id = ZZB_ZCL_BALLAST, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE_LEVEL, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE},
	{.id = ZZB_ZCL_OCCUPANCY},
};

static const struct zzb_zcl_cluster sensor[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE, .server = true},
	{.id = ZZB_ZCL_OCCUPANCY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};
#endif

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x010d,
		.device_version = 2,
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0105,
		.device_version = 1,
		.clusters = switch_clusters,
		.cluster_count = ARRAY_SIZE(switch_clusters),
	},
	{
		.id = 3,
		.profile = 0x0104,
		.device_id = 0x0109,
		.device_version = 1,
		.clusters = ballast_clusters,
		.cluster_count = ARRAY_SIZE(ballast_clusters),
	},
	{
		.id = 4,
		.profile = 0x0104,
		.device_id = 0x0107,
		.device_version = 1,
		.clusters = sensor,
		.cluster_count = ARRAY_SIZE(sensor),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The switch toggles its bound lights after a join */
static void bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
#if defined(CONFIG_ZZB_ZCL)
	if (event == ZZB_BDB_EVENT_STEERING && status == ZZB_BDB_SUCCESS) {
		zzb_zcl_send_bound(node, 2, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE, NULL, 0);
	}
#endif
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
}

static void zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		       uint8_t variant)
{
}

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_effect_cb = zcl_effect,
	};
	struct zzb_node *node;
	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	int err = zzb_node_init(&cfg, &node);

	if (err || !IS_ENABLED(CONFIG_ZZB_BDB)) {
		return err;
	}

	return zzb_bdb_start(node);
}
