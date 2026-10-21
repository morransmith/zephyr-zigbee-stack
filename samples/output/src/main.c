/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(output, LOG_LEVEL_INF);

#define OUTPUT_EP 1

#if defined(CONFIG_SAMPLE_ON_OFF_OUTPUT)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_OUTPUT
#elif defined(CONFIG_SAMPLE_LEVEL_CONTROLLABLE_OUTPUT)
#define DEVICE_ID ZZB_DEVICE_LEVEL_CONTROLLABLE_OUTPUT
#elif defined(CONFIG_SAMPLE_MAINS_POWER_OUTLET)
#define DEVICE_ID ZZB_DEVICE_MAINS_POWER_OUTLET
#elif defined(CONFIG_SAMPLE_ON_OFF_PLUG_IN_UNIT)
#define DEVICE_ID ZZB_DEVICE_ON_OFF_PLUG_IN_UNIT
#elif defined(CONFIG_SAMPLE_DIMMABLE_PLUG_IN_UNIT)
#define DEVICE_ID ZZB_DEVICE_DIMMABLE_PLUG_IN_UNIT
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

static const struct zzb_zcl_cluster output[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
#if defined(CONFIG_SAMPLE_LEVEL)
	ZZB_ZCL_LEVEL_SERVER(&level),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = OUTPUT_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = output,
		.cluster_count = ARRAY_SIZE(output),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* OnOff for the button, which runs outside the stack thread */
static atomic_t relay_on;

static void relay_set(bool on)
{
	atomic_set(&relay_on, on);
	sample_led(on);
	LOG_INF("relay %s", on ? "on" : "off");
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			relay_set(on_off.on_off);
		}
		break;
#if defined(CONFIG_SAMPLE_LEVEL)
	case ZZB_ZCL_LEVEL:
		if (attr == ZZB_ZCL_LEVEL_CURRENT_LEVEL) {
			LOG_INF("level %u", level.current_level);
		}
		break;
#endif
	default:
		break;
	}
}

void sample_button(struct zzb_node *node)
{
	bool on = !atomic_get(&relay_on);
	int err = zzb_zcl_attr_set(node, OUTPUT_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	relay_set(on);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, OUTPUT_EP));
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
	relay_set(on_off.on_off);
	return zzb_bdb_start(node);
}
