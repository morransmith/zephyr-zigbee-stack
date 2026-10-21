/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/binary_input.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

#define SENSOR_EP 1

#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
#define DEVICE_ID ZZB_DEVICE_SIMPLE_SENSOR
#elif defined(CONFIG_SAMPLE_LIGHT_SENSOR)
#define DEVICE_ID ZZB_DEVICE_LIGHT_SENSOR
#elif defined(CONFIG_SAMPLE_OCCUPANCY_SENSOR)
#define DEVICE_ID ZZB_DEVICE_OCCUPANCY_SENSOR
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
#define DEVICE_ID ZZB_DEVICE_LIGHT_LEVEL_SENSOR
#elif defined(CONFIG_SAMPLE_TEMPERATURE_SENSOR)
#define DEVICE_ID ZZB_DEVICE_TEMPERATURE_SENSOR
#elif defined(CONFIG_SAMPLE_PRESSURE_SENSOR)
#define DEVICE_ID ZZB_DEVICE_PRESSURE_SENSOR
#elif defined(CONFIG_SAMPLE_FLOW_SENSOR)
#define DEVICE_ID ZZB_DEVICE_FLOW_SENSOR
#endif

#define MEASURE_PERIOD   K_SECONDS(60)
#define UNOCCUPIED_DELAY K_SECONDS(30)

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

/* Two AA cells, 3.0 V, 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
static struct zzb_zcl_binary_input input = {
	.description = "Contact",
};
#elif defined(CONFIG_SAMPLE_LIGHT_SENSOR)
/* 1 lx to 100 klx */
static struct zzb_zcl_illuminance illuminance = {
	.measured_value = ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE_UNKNOWN,
	.min_measured_value = 1,
	.max_measured_value = 50001,
};
#elif defined(CONFIG_SAMPLE_OCCUPANCY_SENSOR)
static struct zzb_zcl_occupancy occupancy = {
	.occupancy_sensor_type = ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_PIR,
	.occupancy_sensor_type_bitmap = ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP_PIR,
};
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
static struct zzb_zcl_illuminance_level illuminance_level;
#elif defined(CONFIG_SAMPLE_TEMPERATURE_SENSOR)
/* 20.00 to 24.00 degC */
static struct zzb_zcl_temperature temperature = {
	.measured_value = ZZB_ZCL_TEMPERATURE_MEASURED_VALUE_UNKNOWN,
	.min_measured_value = 2000,
	.max_measured_value = 2400,
};
#elif defined(CONFIG_SAMPLE_PRESSURE_SENSOR)
/* 1000 to 1020 hPa */
static struct zzb_zcl_pressure pressure = {
	.measured_value = ZZB_ZCL_PRESSURE_MEASURED_VALUE_UNKNOWN,
	.min_measured_value = 1000,
	.max_measured_value = 1020,
	.scaled_value = ZZB_ZCL_PRESSURE_SCALED_VALUE_UNKNOWN,
	.min_scaled_value = ZZB_ZCL_PRESSURE_MIN_SCALED_VALUE_NOT_DEFINED,
	.max_scaled_value = ZZB_ZCL_PRESSURE_MAX_SCALED_VALUE_NOT_DEFINED,
};
#elif defined(CONFIG_SAMPLE_FLOW_SENSOR)
/* 1.0 to 3.0 m3/h */
static struct zzb_zcl_flow flow = {
	.measured_value = ZZB_ZCL_FLOW_MEASURED_VALUE_UNKNOWN,
	.min_measured_value = 10,
	.max_measured_value = 30,
};
#endif

static const struct zzb_zcl_cluster sensor[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
	ZZB_ZCL_BINARY_INPUT_SERVER(&input),
#elif defined(CONFIG_SAMPLE_LIGHT_SENSOR)
	ZZB_ZCL_ILLUMINANCE_SERVER(&illuminance),
#elif defined(CONFIG_SAMPLE_OCCUPANCY_SENSOR)
	ZZB_ZCL_OCCUPANCY_SERVER(&occupancy),
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
	ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&illuminance_level),
#elif defined(CONFIG_SAMPLE_TEMPERATURE_SENSOR)
	ZZB_ZCL_TEMPERATURE_SERVER(&temperature),
#elif defined(CONFIG_SAMPLE_PRESSURE_SENSOR)
	ZZB_ZCL_PRESSURE_SERVER(&pressure),
#elif defined(CONFIG_SAMPLE_FLOW_SENSOR)
	ZZB_ZCL_FLOW_SERVER(&flow),
#endif
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = SENSOR_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = sensor,
		.cluster_count = ARRAY_SIZE(sensor),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* For the timers */
static struct zzb_node *sensor_node;

#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
/* PresentValue and OutOfService for the button, which runs outside the stack thread */
static atomic_t contact;
static atomic_t out_of_service;

static void contact_set(bool value)
{
	atomic_set(&contact, value);
	sample_led(value);
	LOG_INF("contact %d", value);
}
#elif defined(CONFIG_SAMPLE_OCCUPANCY_SENSOR)
static void occupancy_set(uint8_t value)
{
	int err = zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_OCCUPANCY,
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
#elif defined(CONFIG_SAMPLE_LIGHT_SENSOR)
/* 1 lx to 100 klx in steps of a factor of 10, as 10000 log10(lux) + 1 */
static const uint16_t values[] = {1, 10001, 20001, 30001, 40001, 50001};

static int value_set(const uint16_t *value)
{
	return zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_ILLUMINANCE,
				ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE, value);
}
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
/* Half the width of the dead band around the target, a factor of 1.26 in lux */
#define DEAD_BAND 1000

/* 1 lx to 100 klx in steps of a factor of 10, as 10000 log10(lux) like the target */
static const uint16_t values[] = {0, 10000, 20000, 30000, 40000, 50000};

/* IlluminanceTargetLevel for the timer, which runs outside the stack thread */
static atomic_t target_level;

static uint8_t level_status(uint16_t illuminance)
{
	uint16_t target = atomic_get(&target_level);

	if (target == ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL_UNKNOWN) {
		return ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_ON_TARGET;
	}

	if (illuminance + DEAD_BAND < target) {
		return ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_BELOW_TARGET;
	}

	if (illuminance > target + DEAD_BAND) {
		return ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_ABOVE_TARGET;
	}

	return ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_ON_TARGET;
}

static int value_set(const uint16_t *value)
{
	uint8_t status = level_status(*value);

	LOG_INF("level status %u", status);
	return zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_ILLUMINANCE_LEVEL,
				ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS, &status);
}
#elif defined(CONFIG_SAMPLE_TEMPERATURE_SENSOR)
/* 20.00 to 24.00 degC, as 100 x degC */
static const int16_t values[] = {2000, 2100, 2200, 2300, 2400};

static int value_set(const int16_t *value)
{
	return zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_TEMPERATURE,
				ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, value);
}
#elif defined(CONFIG_SAMPLE_PRESSURE_SENSOR)
/* 1000 to 1020 hPa, as 10 x kPa */
static const int16_t values[] = {1000, 1005, 1010, 1015, 1020};

static int value_set(const int16_t *value)
{
	return zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_PRESSURE,
				ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, value);
}
#elif defined(CONFIG_SAMPLE_FLOW_SENSOR)
/* 1.0 to 3.0 m3/h, as 10 x m3/h */
static const uint16_t values[] = {10, 15, 20, 25, 30};

static int value_set(const uint16_t *value)
{
	return zzb_zcl_attr_set(sensor_node, SENSOR_EP, ZZB_ZCL_FLOW,
				ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, value);
}
#endif

#if defined(CONFIG_SAMPLE_MEASURED)
static void measure(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(measure_work, measure);

/* Sets the next synthetic value and schedules the one after it */
static void measure(struct k_work *work)
{
	static size_t next;
	size_t i = next;
	int err;

	next = (next + 1) % ARRAY_SIZE(values);
	k_work_schedule(&measure_work, MEASURE_PERIOD);

	err = value_set(&values[i]);
	if (err) {
		LOG_ERR("value %d", err);
		return;
	}

	LOG_INF("value %d", values[i]);
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
	case ZZB_ZCL_BINARY_INPUT:
		if (attr == ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE) {
			atomic_set(&out_of_service, input.out_of_service);
			LOG_INF("out of service %d", input.out_of_service);
		}
		/* A write, which OutOfService TRUE allows */
		if (attr == ZZB_ZCL_BINARY_INPUT_PRESENT_VALUE) {
			contact_set(input.present_value);
		}
		break;
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
	case ZZB_ZCL_ILLUMINANCE_LEVEL:
		if (attr == ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL) {
			atomic_set(&target_level, illuminance_level.illuminance_target_level);
			LOG_INF("target level %u", illuminance_level.illuminance_target_level);
		}
		break;
#endif
	default:
		break;
	}
}

#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
void sample_button(struct zzb_node *node)
{
	bool value = !atomic_get(&contact);
	int err;

	if (atomic_get(&out_of_service)) {
		LOG_INF("out of service");
		return;
	}

	err = zzb_zcl_attr_set(node, SENSOR_EP, ZZB_ZCL_BINARY_INPUT,
			       ZZB_ZCL_BINARY_INPUT_PRESENT_VALUE, &value);
	if (err) {
		LOG_ERR("PresentValue %d", err);
		return;
	}

	contact_set(value);
}
#elif defined(CONFIG_SAMPLE_OCCUPANCY_SENSOR)
void sample_button(struct zzb_node *node)
{
	occupancy_set(ZZB_ZCL_OCCUPANCY_OCCUPIED);
	k_work_reschedule(&vacate_work, UNOCCUPIED_DELAY);
}
#else
/* The next synthetic value at once, and the one after it a period later */
void sample_button(struct zzb_node *node)
{
	k_work_reschedule(&measure_work, K_NO_WAIT);
}
#endif

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, SENSOR_EP, ZZB_BDB_FB_UNICAST));
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
	int err = sample_init(&cfg, &sensor_node);

	if (err) {
		return err;
	}

#if defined(CONFIG_SAMPLE_SIMPLE_SENSOR)
	/* OutOfService is loaded from its record */
	atomic_set(&out_of_service, input.out_of_service);
	contact_set(input.present_value);
#elif defined(CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR)
	atomic_set(&target_level, illuminance_level.illuminance_target_level);
#endif
#if defined(CONFIG_SAMPLE_MEASURED)
	k_work_schedule(&measure_work, K_NO_WAIT);
#endif
	return zzb_bdb_start(sensor_node);
}
