/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/pump.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(pump, LOG_LEVEL_INF);

#define PUMP_EP 1

/* 200.0 kPa and 2.0 m3/h while the pump runs */
#define RUNNING_PRESSURE 2000
#define RUNNING_FLOW     20

/* The Running bit of PumpStatus (ZCL Table 6-5) */
#define PUMP_STATUS_RUNNING BIT(5)

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_SLEEPY)
/* 3.0 V, 100 %, two AA cells */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = 0x03,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
#endif

#if defined(CONFIG_SAMPLE_PUMP)
static struct zzb_zcl_scenes scenes;
static struct zzb_zcl_on_off on_off;

static struct zzb_zcl_pump pump = {
	.max_pressure = INT16_MIN,
	.max_speed = 0xffff,
	.max_flow = 0xffff,
	.capacity = INT16_MIN,
	.lifetime_energy_consumed = 0xffffffff,
};

static struct zzb_zcl_pressure pressure = {
	.measured_value = INT16_MIN,
	.min_measured_value = INT16_MIN,
	.max_measured_value = INT16_MIN,
	.scaled_value = INT16_MIN,
	.min_scaled_value = INT16_MIN,
	.max_scaled_value = INT16_MIN,
};

static struct zzb_zcl_flow flow = {
	.measured_value = 0xffff,
	.min_measured_value = 0xffff,
	.max_measured_value = 0xffff,
};
#endif

static const struct zzb_zcl_cluster clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
#if defined(CONFIG_SAMPLE_SLEEPY)
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
#endif
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
#if defined(CONFIG_SAMPLE_PUMP)
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_PUMP_SERVER(&pump),
	ZZB_ZCL_PRESSURE_SERVER(&pressure),
	ZZB_ZCL_FLOW_SERVER(&flow),
#else
	ZZB_ZCL_ON_OFF_CLIENT(),
	ZZB_ZCL_PUMP_CLIENT(),
	ZZB_ZCL_TEMPERATURE_CLIENT(),
	ZZB_ZCL_PRESSURE_CLIENT(),
	ZZB_ZCL_FLOW_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = PUMP_EP,
		.profile = 0x0104,
		.device_id = CONFIG_SAMPLE_DEVICE_ID,
		.device_version = CONFIG_SAMPLE_DEVICE_VERSION,
		.clusters = clusters,
		.cluster_count = ARRAY_SIZE(clusters),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

#if defined(CONFIG_SAMPLE_PUMP)
/* OnOff for the button, which runs outside the stack thread */
static atomic_t pump_on;

static void pump_value_set(struct zzb_node *node, uint16_t cluster, uint16_t attr,
			   const void *value)
{
	int err = zzb_zcl_attr_set(node, PUMP_EP, cluster, attr, value);

	if (err) {
		LOG_ERR("cluster 0x%04x attribute 0x%04x %d", cluster, attr, err);
	}
}

/* The pressure, the flow and PumpStatus follow OnOff */
static void pump_set(struct zzb_node *node, bool on)
{
	int16_t measured_pressure = on ? RUNNING_PRESSURE : 0;
	uint16_t measured_flow = on ? RUNNING_FLOW : 0;
	uint16_t status = on ? PUMP_STATUS_RUNNING : 0;

	atomic_set(&pump_on, on);
	sample_led(on);
	LOG_INF("pump %s", on ? "on" : "off");
	pump_value_set(node, ZZB_ZCL_PRESSURE, ZZB_ZCL_MEASUREMENT_MEASURED_VALUE,
		       &measured_pressure);
	pump_value_set(node, ZZB_ZCL_FLOW, ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, &measured_flow);
	pump_value_set(node, ZZB_ZCL_PUMP, ZZB_ZCL_PUMP_STATUS, &status);
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
#if defined(CONFIG_SAMPLE_PUMP)
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			pump_set(node, on_off.on_off);
		}
		break;
#endif
	default:
		break;
	}
}

void sample_button(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_PUMP)
	bool on = !atomic_get(&pump_on);
	int err = zzb_zcl_attr_set(node, PUMP_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	pump_set(node, on);
#else
	int err = zzb_zcl_send_bound(node, PUMP_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE, NULL, 0);

	if (err) {
		LOG_ERR("Toggle %d", err);
		return;
	}

	LOG_INF("Toggle");
#endif
}

void sample_finding_binding(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_PUMP)
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, PUMP_EP));
#else
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_initiator(node, PUMP_EP, 0xffff));
#endif
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

#if defined(CONFIG_SAMPLE_PUMP)
	/* StartUpOnOff has set OnOff without zcl_cb */
	pump_set(node, on_off.on_off);
#endif
	return zzb_bdb_start(node);
}
