/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/on_off_switch_config.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(switch, LOG_LEVEL_INF);

#define SWITCH_EP 1

#if defined(CONFIG_SAMPLE_ON_OFF_SWITCH)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_SWITCH
#elif defined(CONFIG_SAMPLE_LEVEL_CONTROL_SWITCH)
#define DEVICE_ID ZZB_DEVICE_LEVEL_CONTROL_SWITCH
#elif defined(CONFIG_SAMPLE_ON_OFF_LIGHT_SWITCH)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_LIGHT_SWITCH
#elif defined(CONFIG_SAMPLE_DIMMER_SWITCH)
#define DEVICE_ID ZZB_DEVICE_DIMMER_SWITCH
#elif defined(CONFIG_SAMPLE_COLOR_DIMMER_SWITCH)
#define DEVICE_ID ZZB_DEVICE_COLOR_DIMMER_SWITCH
#endif

/* The levels and color temperatures in mireds that the presses send in turn, over 1 s */
#define LEVEL_LOW        50
#define LEVEL_HIGH       200
#define TEMPERATURE_COLD 250
#define TEMPERATURE_WARM 400
#define TRANSITION_TIME  10

/* Short presses send in turn one command of each client of the type except Identify */
#define CYCLE_LENGTH (1 + IS_ENABLED(CONFIG_SAMPLE_LEVEL) + IS_ENABLED(CONFIG_SAMPLE_COLOR))

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

/* Two AA cells of 1.5 V, at 3.0 V and 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};

static struct zzb_zcl_identify identify;

static struct zzb_zcl_on_off_switch_config switch_config = {
	.switch_type = ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_TYPE_MOMENTARY,
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SERVER(&switch_config),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
#if defined(CONFIG_SAMPLE_LEVEL)
	ZZB_ZCL_LEVEL_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_COLOR)
	ZZB_ZCL_COLOR_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = SWITCH_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = switch_clusters,
		.cluster_count = ARRAY_SIZE(switch_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* SwitchActions for the button, which runs outside the stack thread */
static atomic_t switch_actions;

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_ON_OFF_SWITCH_CONFIG:
		if (attr == ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS) {
			atomic_set(&switch_actions, switch_config.switch_actions);
			LOG_INF("SwitchActions %u", switch_config.switch_actions);
		}
		break;
	default:
		break;
	}
}

/* Toggle for SwitchActions Toggle, otherwise On and Off in turn, starting with Off for Off/On */
static uint8_t on_off(unsigned int round)
{
	atomic_val_t actions = atomic_get(&switch_actions);

	if (actions == ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS_TOGGLE) {
		return ZZB_ZCL_ON_OFF_TOGGLE;
	}

	if (actions == ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS_OFF_ON) {
		round++;
	}

	return round % 2 ? ZZB_ZCL_ON_OFF_OFF : ZZB_ZCL_ON_OFF_ON;
}

void sample_button(struct zzb_node *node)
{
	static unsigned int press;
	unsigned int step = press % CYCLE_LENGTH;
	unsigned int round = press / CYCLE_LENGTH;
	int err;

	press++;
	switch (step) {
	case 0:
		err = zzb_zcl_send_bound(node, SWITCH_EP, ZZB_ZCL_ON_OFF, on_off(round), NULL, 0);
		break;
	case 1:
		err = zzb_zcl_level_move_to_level_on_off(node, SWITCH_EP,
							 round % 2 ? LEVEL_HIGH : LEVEL_LOW,
							 TRANSITION_TIME);
		break;
	default:
		err = zzb_zcl_color_move_to_temperature(node, SWITCH_EP,
							round % 2 ? TEMPERATURE_WARM : TEMPERATURE_COLD,
							TRANSITION_TIME);
		break;
	}

	if (err) {
		LOG_ERR("command %u: %d", step, err);
	}
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, SWITCH_EP, ZZB_BDB_FB_UNICAST));
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

	/* The records have set SwitchActions without zcl_cb */
	atomic_set(&switch_actions, switch_config.switch_actions);
	return zzb_bdb_start(node);
}
