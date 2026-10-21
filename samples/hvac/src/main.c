/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/fan.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/thermostat.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(hvac, LOG_LEVEL_INF);

#define HVAC_EP 1

#if defined(CONFIG_SAMPLE_THERMOSTAT)
#define DEVICE_ID ZZB_DEVICE_THERMOSTAT
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
#define DEVICE_ID ZZB_DEVICE_HEATING_COOLING_UNIT
#elif defined(CONFIG_SAMPLE_AIR_PURIFIER)
#define DEVICE_ID ZZB_DEVICE_AIR_PURIFIER
#endif

/* LocalTemperature moves 0.10 degC a minute toward the setpoint of the mode, or to 18.00 degC */
#define START_TEMPERATURE  2100
#define OFF_TEMPERATURE    1800
#define TEMPERATURE_STEP   10
#define MEASUREMENT_PERIOD K_SECONDS(60)

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify identify;

static struct zzb_zcl_fan fan = {
	.fan_mode_sequence = ZZB_ZCL_FAN_MODE_SEQUENCE_LOW_MED_HIGH_AUTO,
};

#if defined(CONFIG_SAMPLE_THERMOSTAT)
static struct zzb_zcl_thermostat thermostat = {
	.local_temperature = START_TEMPERATURE,
	.outdoor_temperature = ZZB_ZCL_THERMOSTAT_OUTDOOR_TEMPERATURE_UNKNOWN,
	.occupancy = ZZB_ZCL_THERMOSTAT_OCCUPANCY_OCCUPIED,
	.abs_min_heat_setpoint_limit = 700,
	.abs_max_heat_setpoint_limit = 3000,
	.abs_min_cool_setpoint_limit = 1600,
	.abs_max_cool_setpoint_limit = 3200,
	.min_setpoint_dead_band = 25,
	.control_sequence_of_operation = ZZB_ZCL_SEQUENCE_COOLING_HEATING,
	.system_mode_default = ZZB_ZCL_SYSTEM_MODE_OFF,
};

static struct zzb_zcl_thermostat_ui thermostat_ui;
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
static struct zzb_zcl_on_off on_off;
#endif

static const struct zzb_zcl_cluster clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
#if defined(CONFIG_SAMPLE_THERMOSTAT)
	ZZB_ZCL_THERMOSTAT_SERVER(&thermostat),
	ZZB_ZCL_FAN_SERVER(&fan),
	ZZB_ZCL_THERMOSTAT_UI_SERVER(&thermostat_ui),
	ZZB_ZCL_IDENTIFY_CLIENT(),
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_FAN_SERVER(&fan),
	ZZB_ZCL_THERMOSTAT_CLIENT(),
#else
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_FAN_SERVER(&fan),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = HVAC_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = clusters,
		.cluster_count = ARRAY_SIZE(clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

#if defined(CONFIG_SAMPLE_THERMOSTAT)
static struct zzb_node *measurement_node;

/* SystemMode and the setpoints for the timer and the button, outside the stack thread */
static atomic_t system_mode;
static atomic_t heating_setpoint;
static atomic_t cooling_setpoint;

static void measure(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(measure_work, measure);

static void thermostat_set(uint16_t attr, const void *value)
{
	int err = zzb_zcl_attr_set(measurement_node, HVAC_EP, ZZB_ZCL_THERMOSTAT, attr, value);

	if (err) {
		LOG_ERR("attribute 0x%04x %d", attr, err);
	}
}

/* Heat and Cool run at full demand until LocalTemperature reaches their setpoint */
static void measure(struct k_work *work)
{
	static int16_t temperature = START_TEMPERATURE;
	uint8_t mode = atomic_get(&system_mode);
	int16_t target;
	bool heating;
	bool cooling;
	uint8_t heating_demand;
	uint8_t cooling_demand;
	uint8_t running_mode;
	uint16_t running_state;

	if (mode == ZZB_ZCL_SYSTEM_MODE_HEAT) {
		target = atomic_get(&heating_setpoint);
		running_mode = ZZB_ZCL_THERMOSTAT_RUNNING_MODE_HEAT;
	} else if (mode == ZZB_ZCL_SYSTEM_MODE_COOL) {
		target = atomic_get(&cooling_setpoint);
		running_mode = ZZB_ZCL_THERMOSTAT_RUNNING_MODE_COOL;
	} else {
		target = OFF_TEMPERATURE;
		running_mode = ZZB_ZCL_THERMOSTAT_RUNNING_MODE_OFF;
	}

	temperature = temperature < target ? MIN(temperature + TEMPERATURE_STEP, target)
					   : MAX(temperature - TEMPERATURE_STEP, target);
	heating = mode == ZZB_ZCL_SYSTEM_MODE_HEAT && temperature < target;
	cooling = mode == ZZB_ZCL_SYSTEM_MODE_COOL && temperature > target;
	heating_demand = heating ? 100 : 0;
	cooling_demand = cooling ? 100 : 0;
	running_state = heating   ? ZZB_ZCL_THERMOSTAT_RUNNING_STATE_HEAT
			: cooling ? ZZB_ZCL_THERMOSTAT_RUNNING_STATE_COOL
				  : 0;

	thermostat_set(ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE, &temperature);
	thermostat_set(ZZB_ZCL_THERMOSTAT_PI_HEATING_DEMAND, &heating_demand);
	thermostat_set(ZZB_ZCL_THERMOSTAT_PI_COOLING_DEMAND, &cooling_demand);
	thermostat_set(ZZB_ZCL_THERMOSTAT_RUNNING_MODE, &running_mode);
	thermostat_set(ZZB_ZCL_THERMOSTAT_RUNNING_STATE, &running_state);
	LOG_INF("temperature %d.%02d degC, demand heating %u %% cooling %u %%", temperature / 100,
		temperature % 100, heating_demand, cooling_demand);
	k_work_schedule(&measure_work, MEASUREMENT_PERIOD);
}

static void mode_set(uint8_t mode)
{
	atomic_set(&system_mode, mode);
	sample_led(mode != ZZB_ZCL_SYSTEM_MODE_OFF);
	LOG_INF("system mode %u", mode);
}

/* Takes SystemMode and the setpoints from the value struct */
static void thermostat_changed(void)
{
	atomic_set(&heating_setpoint, thermostat.occupied_heating_setpoint);
	atomic_set(&cooling_setpoint, thermostat.occupied_cooling_setpoint);
	LOG_INF("setpoints heating %d cooling %d", thermostat.occupied_heating_setpoint,
		thermostat.occupied_cooling_setpoint);
	mode_set(thermostat.system_mode);
}
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
/* OnOff for the button, which runs outside the stack thread */
static atomic_t unit_on;

static void unit_set(bool on)
{
	atomic_set(&unit_on, on);
	sample_led(on);
	LOG_INF("unit %s", on ? "on" : "off");
}
#else
/* FanMode for the button, which runs outside the stack thread */
static atomic_t fan_mode;

static void fan_set(uint8_t mode)
{
	atomic_set(&fan_mode, mode);
	sample_led(mode != ZZB_ZCL_FAN_MODE_OFF);
	LOG_INF("fan mode %u", mode);
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_FAN:
#if defined(CONFIG_SAMPLE_AIR_PURIFIER)
		fan_set(fan.fan_mode);
#else
		LOG_INF("fan mode %u", fan.fan_mode);
#endif
		break;
#if defined(CONFIG_SAMPLE_THERMOSTAT)
	case ZZB_ZCL_THERMOSTAT:
		thermostat_changed();
		break;
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			unit_set(on_off.on_off);
		}
		break;
#endif
	default:
		break;
	}
}

#if defined(CONFIG_SAMPLE_THERMOSTAT)
void sample_button(struct zzb_node *node)
{
	uint8_t mode = atomic_get(&system_mode) == ZZB_ZCL_SYSTEM_MODE_OFF
			       ? ZZB_ZCL_SYSTEM_MODE_HEAT
			       : ZZB_ZCL_SYSTEM_MODE_OFF;
	int err = zzb_zcl_attr_set(node, HVAC_EP, ZZB_ZCL_THERMOSTAT,
				   ZZB_ZCL_THERMOSTAT_SYSTEM_MODE, &mode);

	if (err) {
		LOG_ERR("SystemMode %d", err);
		return;
	}

	mode_set(mode);
}
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
void sample_button(struct zzb_node *node)
{
	bool on = !atomic_get(&unit_on);
	int err = zzb_zcl_attr_set(node, HVAC_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	unit_set(on);
}
#else
/* Off and the modes of the FanModeSequence in the order of the button */
static const uint8_t fan_modes[] = {
	ZZB_ZCL_FAN_MODE_OFF,
	ZZB_ZCL_FAN_MODE_LOW,
	ZZB_ZCL_FAN_MODE_MEDIUM,
	ZZB_ZCL_FAN_MODE_HIGH,
	ZZB_ZCL_FAN_MODE_AUTO,
};

void sample_button(struct zzb_node *node)
{
	uint8_t mode = fan_modes[0];
	int err;

	/* Off follows the last mode and a mode outside the sequence */
	for (size_t i = 0; i + 1 < ARRAY_SIZE(fan_modes); i++) {
		if (fan_modes[i] == atomic_get(&fan_mode)) {
			mode = fan_modes[i + 1];
		}
	}

	err = zzb_zcl_attr_set(node, HVAC_EP, ZZB_ZCL_FAN, ZZB_ZCL_FAN_MODE, &mode);
	if (err) {
		LOG_ERR("FanMode %d", err);
		return;
	}

	fan_set(mode);
}
#endif

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, HVAC_EP));
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

#if defined(CONFIG_SAMPLE_THERMOSTAT)
	measurement_node = node;
	thermostat_changed();
	k_work_schedule(&measure_work, MEASUREMENT_PERIOD);
#elif defined(CONFIG_SAMPLE_HEATING_COOLING_UNIT)
	/* StartUpOnOff has set OnOff without zcl_cb */
	unit_set(on_off.on_off);
#else
	fan_set(fan.fan_mode);
#endif
	return zzb_bdb_start(node);
}
