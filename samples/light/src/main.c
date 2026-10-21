/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(light, LOG_LEVEL_INF);

#define LIGHT_EP 1

#if defined(CONFIG_SAMPLE_ON_OFF_LIGHT)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_LIGHT
#elif defined(CONFIG_SAMPLE_DIMMABLE_LIGHT)
#define DEVICE_ID ZZB_DEVICE_DIMMABLE_LIGHT
#elif defined(CONFIG_SAMPLE_COLOR_DIMMABLE_LIGHT)
#define DEVICE_ID ZZB_DEVICE_COLOR_DIMMABLE_LIGHT
#elif defined(CONFIG_SAMPLE_COLOR_TEMPERATURE_LIGHT)
#define DEVICE_ID ZZB_DEVICE_COLOR_TEMPERATURE_LIGHT
#elif defined(CONFIG_SAMPLE_EXTENDED_COLOR_LIGHT)
#define DEVICE_ID ZZB_DEVICE_EXTENDED_COLOR_LIGHT
#endif

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify identify;
static struct zzb_zcl_scenes scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

#if defined(CONFIG_SAMPLE_LEVEL)
static struct zzb_zcl_level level = {
	.start_up_default = ZZB_ZCL_LEVEL_START_UP_PREVIOUS,
};
#endif

#if defined(CONFIG_SAMPLE_COLOR)
/* Color temperature from 6500 K to 2000 K, in mireds */
static struct zzb_zcl_color color = {
	.color_capabilities =
		(IS_ENABLED(CONFIG_SAMPLE_COLOR_XY) ? ZZB_ZCL_COLOR_CAP_XY : 0) |
		(IS_ENABLED(CONFIG_SAMPLE_COLOR_TEMPERATURE) ? ZZB_ZCL_COLOR_CAP_TEMPERATURE : 0),
	.color_temp_physical_min = 153,
	.color_temp_physical_max = 500,
	.couple_color_temp_min = 153,
	.start_up_default = ZZB_ZCL_COLOR_START_UP_TEMPERATURE_PREVIOUS,
};
#endif

static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
#if defined(CONFIG_SAMPLE_LEVEL)
	ZZB_ZCL_LEVEL_SERVER(&level),
#endif
#if defined(CONFIG_SAMPLE_COLOR)
	ZZB_ZCL_COLOR_SERVER(&color),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = LIGHT_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* OnOff for the button, which runs outside the stack thread */
static atomic_t light_on;

static void light_set(bool on)
{
	atomic_set(&light_on, on);
	sample_led(on);
	LOG_INF("light %s", on ? "on" : "off");
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			light_set(on_off.on_off);
		}
		break;
#if defined(CONFIG_SAMPLE_LEVEL)
	case ZZB_ZCL_LEVEL:
		if (attr == ZZB_ZCL_LEVEL_CURRENT_LEVEL) {
			LOG_INF("level %u", level.current_level);
		}
		break;
#endif
#if defined(CONFIG_SAMPLE_COLOR)
	case ZZB_ZCL_COLOR:
		if (attr == ZZB_ZCL_COLOR_CURRENT_X || attr == ZZB_ZCL_COLOR_CURRENT_Y ||
		    attr == ZZB_ZCL_COLOR_TEMPERATURE) {
			LOG_INF("color x %u y %u temperature %u mireds", color.current_x,
				color.current_y, color.color_temperature);
		}
		break;
#endif
	default:
		break;
	}
}

void sample_button(struct zzb_node *node)
{
	bool on = !atomic_get(&light_on);
	int err = zzb_zcl_attr_set(node, LIGHT_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	light_set(on);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, LIGHT_EP));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

	/* StartUpOnOff has set OnOff without zcl_cb */
	light_set(on_off.on_off);
	return zzb_bdb_start(node);
}
