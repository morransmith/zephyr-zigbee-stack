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

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

#if defined(CONFIG_SAMPLE_SLEEPY)
/* Two AA cells, 3.0 V, 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = 0x03,
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
		.profile = 0x0104,
		.device_id = CONFIG_SAMPLE_DEVICE_ID,
		.device_version = CONFIG_SAMPLE_DEVICE_VERSION,
		.clusters = controller,
		.cluster_count = ARRAY_SIZE(controller),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* A cluster-specific command with its payload in ZCL order, little endian */
struct command {
	uint16_t cluster;
	uint8_t id;
	uint8_t payload[4];
	uint8_t len;
};

/*
 * The commands that the short presses send in turn. The transition time of Move to Level with
 * On/Off and Move to Color Temperature is 10 tenths of a second, and the scenes are in group 0.
 */
static const struct command commands[] = {
#if defined(CONFIG_SAMPLE_SCENE_SELECTOR)
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_RECALL_SCENE, {0x00, 0x00, 1}, 3},
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_RECALL_SCENE, {0x00, 0x00, 2}, 3},
#elif defined(CONFIG_SAMPLE_CONFIGURATION_TOOL)
	/* Group 0x0001 with an empty name */
	{ZZB_ZCL_GROUPS, ZZB_ZCL_GROUPS_ADD_GROUP, {0x01, 0x00, 0}, 3},
	{ZZB_ZCL_GROUPS, ZZB_ZCL_GROUPS_REMOVE_GROUP, {0x01, 0x00}, 2},
#elif defined(CONFIG_SAMPLE_ON_OFF_SENSOR)
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
#elif defined(CONFIG_SAMPLE_NON_COLOR_CONTROLLER)
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {50, 10, 0}, 3},
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {200, 10, 0}, 3},
#elif defined(CONFIG_SAMPLE_NON_COLOR_SCENE_CONTROLLER)
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {50, 10, 0}, 3},
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_STORE_SCENE, {0x00, 0x00, 1}, 3},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {200, 10, 0}, 3},
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_RECALL_SCENE, {0x00, 0x00, 1}, 3},
#elif defined(CONFIG_SAMPLE_COLOR_SCENE_CONTROLLER)
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {50, 10, 0}, 3},
	/* 250 mireds */
	{ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, {0xfa, 0x00, 10, 0}, 4},
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_STORE_SCENE, {0x00, 0x00, 1}, 3},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {200, 10, 0}, 3},
	/* 400 mireds */
	{ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, {0x90, 0x01, 10, 0}, 4},
	{ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_RECALL_SCENE, {0x00, 0x00, 1}, 3},
#else
	/* Remote Control, Combined Interface, Color Controller and Control Bridge */
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {50, 10, 0}, 3},
	/* 250 mireds */
	{ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, {0xfa, 0x00, 10, 0}, 4},
	{ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE},
	{ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF, {200, 10, 0}, 3},
	/* 400 mireds */
	{ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, {0x90, 0x01, 10, 0}, 4},
#endif
};

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(identify.identify_time > 0);
	}
}

void sample_button(struct zzb_node *node)
{
	static size_t next;
	const struct command *cmd = &commands[next];
	int err;

	next = (next + 1) % ARRAY_SIZE(commands);
	err = zzb_zcl_send_bound(node, CONTROLLER_EP, cmd->cluster, cmd->id, cmd->payload,
				 cmd->len);
	if (err) {
		LOG_ERR("cluster 0x%04x command 0x%02x %d", cmd->cluster, cmd->id, err);
		return;
	}

	LOG_INF("cluster 0x%04x command 0x%02x sent", cmd->cluster, cmd->id);
}

/* Group 0xffff makes unicast bindings */
void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, CONTROLLER_EP, 0xffff));
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
