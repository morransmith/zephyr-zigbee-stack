/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(multisensor, LOG_LEVEL_INF);

#define OCCUPANCY_EP   1
#define LIGHT_EP       2
#define TEMPERATURE_EP 3

#define MEASURE_PERIOD   K_SECONDS(60)
#define UNOCCUPIED_DELAY K_SECONDS(30)

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "multisensor",
	.power_source = 0x03,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

/* Two AA cells, 3.0 V, 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = 0x03,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};

static struct zzb_zcl_identify occupancy_identify;
static struct zzb_zcl_identify light_identify;
static struct zzb_zcl_identify temperature_identify;

/* PIR */
static struct zzb_zcl_occupancy occupancy = {
	.occupancy_sensor_type_bitmap = 0x01,
};

/* 1 lx to 10 klx */
static struct zzb_zcl_illuminance illuminance = {
	.measured_value = 0xffff,
	.min_measured_value = 1,
	.max_measured_value = 40001,
};

/* 20.00 to 24.00 degC */
static struct zzb_zcl_temperature temperature = {
	.measured_value = INT16_MIN,
	.min_measured_value = 2000,
	.max_measured_value = 2400,
};

/* 40 to 60 % */
static struct zzb_zcl_humidity humidity = {
	.measured_value = 0xffff,
	.min_measured_value = 4000,
	.max_measured_value = 6000,
};

static const struct zzb_zcl_cluster occupancy_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_IDENTIFY_SERVER(&occupancy_identify),
	ZZB_ZCL_OCCUPANCY_SERVER(&occupancy),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster light_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&light_identify),
	ZZB_ZCL_ILLUMINANCE_SERVER(&illuminance),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster temperature_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&temperature_identify),
	ZZB_ZCL_TEMPERATURE_SERVER(&temperature),
	ZZB_ZCL_HUMIDITY_SERVER(&humidity),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

/* An Occupancy Sensor, a Light Sensor and a Temperature Sensor with humidity */
static const struct zzb_endpoint endpoints[] = {
	{
		.id = OCCUPANCY_EP,
		.profile = 0x0104,
		.device_id = 0x0107,
		.device_version = 1,
		.clusters = occupancy_clusters,
		.cluster_count = ARRAY_SIZE(occupancy_clusters),
	},
	{
		.id = LIGHT_EP,
		.profile = 0x0104,
		.device_id = 0x0106,
		.device_version = 1,
		.clusters = light_clusters,
		.cluster_count = ARRAY_SIZE(light_clusters),
	},
	{
		.id = TEMPERATURE_EP,
		.profile = 0x0104,
		.device_id = 0x0302,
		.device_version = 1,
		.clusters = temperature_clusters,
		.cluster_count = ARRAY_SIZE(temperature_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_DISPOSABLE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* For the timers */
static struct zzb_node *sensor_node;

/* The synthetic values, set together; illuminance is 10000 log10(lux) + 1 */
static const uint16_t lux_values[] = {1, 10001, 20001, 30001, 40001};
static const int16_t temperature_values[] = {2000, 2100, 2200, 2300, 2400};
static const uint16_t humidity_values[] = {4000, 4500, 5000, 5500, 6000};

static void value_set(uint8_t ep, uint16_t cluster, const void *value)
{
	int err = zzb_zcl_attr_set(sensor_node, ep, cluster, ZZB_ZCL_MEASUREMENT_MEASURED_VALUE,
				   value);

	if (err) {
		LOG_ERR("cluster 0x%04x %d", cluster, err);
	}
}

static void measure(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(measure_work, measure);

static void measure(struct k_work *work)
{
	static size_t next;
	size_t i = next;

	next = (next + 1) % ARRAY_SIZE(lux_values);
	k_work_schedule(&measure_work, MEASURE_PERIOD);

	value_set(LIGHT_EP, ZZB_ZCL_ILLUMINANCE, &lux_values[i]);
	value_set(TEMPERATURE_EP, ZZB_ZCL_TEMPERATURE, &temperature_values[i]);
	value_set(TEMPERATURE_EP, ZZB_ZCL_HUMIDITY, &humidity_values[i]);
	LOG_INF("illuminance %u temperature %d humidity %u", lux_values[i], temperature_values[i],
		humidity_values[i]);
}

static void occupancy_set(uint8_t value)
{
	int err = zzb_zcl_attr_set(sensor_node, OCCUPANCY_EP, ZZB_ZCL_OCCUPANCY,
				   ZZB_ZCL_OCCUPANCY_OCCUPANCY, &value);

	if (err) {
		LOG_ERR("Occupancy %d", err);
		return;
	}

	sample_led(value);
	LOG_INF("occupancy %u", value);
}

static void vacate(struct k_work *work)
{
	occupancy_set(0);
}

static K_WORK_DELAYABLE_DEFINE(vacate_work, vacate);

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(occupancy_identify.identify_time > 0 ||
				light_identify.identify_time > 0 ||
				temperature_identify.identify_time > 0);
	}
}

/* Motion: occupied until 30 s after the last press */
void sample_button(struct zzb_node *node)
{
	occupancy_set(1);
	k_work_reschedule(&vacate_work, UNOCCUPIED_DELAY);
}

/* Group 0xffff makes unicast bindings */
void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, OCCUPANCY_EP, 0xffff));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
		.sleepy = true,
	};
	int err = sample_init(&cfg, &sensor_node);

	if (err) {
		return err;
	}

	k_work_schedule(&measure_work, K_NO_WAIT);
	return zzb_bdb_start(sensor_node);
}
