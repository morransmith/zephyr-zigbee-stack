/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/color.h>
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

LOG_MODULE_REGISTER(controller, LOG_LEVEL_INF);

#define CONTROLLER_EP 1

#if defined(CONFIG_SAMPLE_SCENE_SELECTOR)
#define DEVICE_ID ZZB_DEVICE_SCENE_SELECTOR
#elif defined(CONFIG_SAMPLE_CONFIGURATION_TOOL)
#define DEVICE_ID ZZB_DEVICE_CONFIGURATION_TOOL
#elif defined(CONFIG_SAMPLE_REMOTE_CONTROL)
#define DEVICE_ID ZZB_DEVICE_REMOTE_CONTROL
#elif defined(CONFIG_SAMPLE_COMBINED_INTERFACE)
#define DEVICE_ID ZZB_DEVICE_COMBINED_INTERFACE
#elif defined(CONFIG_SAMPLE_COLOR_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_COLOR_CONTROLLER
#elif defined(CONFIG_SAMPLE_COLOR_SCENE_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_COLOR_SCENE_CONTROLLER
#elif defined(CONFIG_SAMPLE_NON_COLOR_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_NON_COLOR_CONTROLLER
#elif defined(CONFIG_SAMPLE_NON_COLOR_SCENE_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_NON_COLOR_SCENE_CONTROLLER
#elif defined(CONFIG_SAMPLE_CONTROL_BRIDGE)
#define DEVICE_ID ZZB_DEVICE_CONTROL_BRIDGE
#elif defined(CONFIG_SAMPLE_ON_OFF_SENSOR)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_SENSOR
#endif

/* Levels, color temperatures in mireds and the transition time of 1 s that the presses send */
#define DIM_LEVEL       50
#define BRIGHT_LEVEL    200
#define COLD_MIREDS     250
#define WARM_MIREDS     400
#define TRANSITION_TIME 10

/* The scenes of the scene controllers and the group of the Configuration Tool */
#define SCENE   1
#define SCENE_2 2
#define GROUP   0x0001

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

static const struct zzb_zcl_cluster controller[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
#if defined(CONFIG_SAMPLE_SLEEPY)
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
#endif
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
#if defined(CONFIG_SAMPLE_GROUPS)
	ZZB_ZCL_GROUPS_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_SCENES)
	ZZB_ZCL_SCENES_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_ON_OFF)
	ZZB_ZCL_ON_OFF_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_LEVEL)
	ZZB_ZCL_LEVEL_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_COLOR)
	ZZB_ZCL_COLOR_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_SENSING)
	ZZB_ZCL_ILLUMINANCE_CLIENT(),
	ZZB_ZCL_ILLUMINANCE_LEVEL_CLIENT(),
	ZZB_ZCL_OCCUPANCY_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = CONTROLLER_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = controller,
		.cluster_count = ARRAY_SIZE(controller),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The commands of the presses */
enum step {
	TOGGLE,
	LEVEL_LOW,
	LEVEL_HIGH,
	TEMPERATURE_COLD,
	TEMPERATURE_WARM,
	STORE_SCENE,
	RECALL_SCENE,
	RECALL_SCENE_2,
	ADD_GROUP,
	REMOVE_GROUP,
};

/* The commands that the short presses send in turn */
static const enum step steps[] = {
#if defined(CONFIG_SAMPLE_SCENE_SELECTOR)
	RECALL_SCENE,
	RECALL_SCENE_2,
#elif defined(CONFIG_SAMPLE_CONFIGURATION_TOOL)
	ADD_GROUP,
	REMOVE_GROUP,
#elif defined(CONFIG_SAMPLE_ON_OFF_SENSOR)
	TOGGLE,
#elif defined(CONFIG_SAMPLE_NON_COLOR_CONTROLLER)
	TOGGLE,
	LEVEL_LOW,
	TOGGLE,
	LEVEL_HIGH,
#elif defined(CONFIG_SAMPLE_NON_COLOR_SCENE_CONTROLLER)
	TOGGLE,
	LEVEL_LOW,
	STORE_SCENE,
	LEVEL_HIGH,
	RECALL_SCENE,
#elif defined(CONFIG_SAMPLE_COLOR_SCENE_CONTROLLER)
	TOGGLE,
	LEVEL_LOW,
	TEMPERATURE_COLD,
	STORE_SCENE,
	LEVEL_HIGH,
	TEMPERATURE_WARM,
	RECALL_SCENE,
#else
	/* Remote Control, Combined Interface, Color Controller and Control Bridge */
	TOGGLE,
	LEVEL_LOW,
	TEMPERATURE_COLD,
	TOGGLE,
	LEVEL_HIGH,
	TEMPERATURE_WARM,
#endif
};

static int send(struct zzb_node *node, enum step step)
{
	switch (step) {
	case TOGGLE:
		return zzb_zcl_send_bound(node, CONTROLLER_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE,
					  NULL, 0);
	case LEVEL_LOW:
		return zzb_zcl_level_move_to_level_on_off(node, CONTROLLER_EP, DIM_LEVEL,
							  TRANSITION_TIME);
	case LEVEL_HIGH:
		return zzb_zcl_level_move_to_level_on_off(node, CONTROLLER_EP, BRIGHT_LEVEL,
							  TRANSITION_TIME);
	case TEMPERATURE_COLD:
		return zzb_zcl_color_move_to_temperature(node, CONTROLLER_EP, COLD_MIREDS,
							 TRANSITION_TIME);
	case TEMPERATURE_WARM:
		return zzb_zcl_color_move_to_temperature(node, CONTROLLER_EP, WARM_MIREDS,
							 TRANSITION_TIME);
	case STORE_SCENE:
		return zzb_zcl_scenes_store_scene(node, CONTROLLER_EP, ZZB_ZCL_SCENES_GROUP_NONE,
						  SCENE);
	case RECALL_SCENE:
		return zzb_zcl_scenes_recall_scene(node, CONTROLLER_EP, ZZB_ZCL_SCENES_GROUP_NONE,
						   SCENE);
	case RECALL_SCENE_2:
		return zzb_zcl_scenes_recall_scene(node, CONTROLLER_EP, ZZB_ZCL_SCENES_GROUP_NONE,
						   SCENE_2);
	case ADD_GROUP:
		return zzb_zcl_groups_add_group(node, CONTROLLER_EP, GROUP, NULL, 0);
	default:
		return zzb_zcl_groups_remove_group(node, CONTROLLER_EP, GROUP);
	}
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(identify.identify_time > 0);
	}
}

void sample_button(struct zzb_node *node)
{
	static size_t next;
	enum step step = steps[next];
	int err;

	next = (next + 1) % ARRAY_SIZE(steps);
	err = send(node, step);
	if (err) {
		LOG_ERR("step %d: %d", step, err);
		return;
	}

	LOG_INF("step %d sent", step);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, CONTROLLER_EP, ZZB_BDB_FB_UNICAST));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
		.sleepy = IS_ENABLED(CONFIG_SAMPLE_SLEEPY),
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

	return zzb_bdb_start(node);
}
