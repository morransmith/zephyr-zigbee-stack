/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/electrical_measurement.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/metering.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zcl/time.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(power_strip, LOG_LEVEL_INF);

/* Outlet i is on endpoint i + 1 */
#define OUTLET_COUNT 2

/* The mains voltage in V and the period of the meter in seconds */
#define VOLTAGE        230
#define METER_PERIOD_S 10

/* The load of each outlet in W and its current in mA at 230 V */
static const int16_t loads[OUTLET_COUNT] = {60, 1500};
static const uint16_t currents[OUTLET_COUNT] = {261, 6522};

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "power_strip",
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify identify[OUTLET_COUNT];
static struct zzb_zcl_scenes scenes[OUTLET_COUNT];

static struct zzb_zcl_on_off outlets[OUTLET_COUNT] = {
	{.start_up_default = ZZB_ZCL_START_UP_PREVIOUS},
	{.start_up_default = ZZB_ZCL_START_UP_PREVIOUS},
};

/* kWh and kW with three decimals; outlet 2 takes the values of outlet 1 in main() */
static struct zzb_zcl_metering metering[OUTLET_COUNT] = {
	{
		.unit_of_measure = ZZB_ZCL_METERING_UNIT_OF_MEASURE_KWH,
		.multiplier = 1,
		.divisor = 1000,
		.summation_formatting = ZZB_ZCL_METERING_FORMATTING(5, 3, false),
		.demand_formatting = ZZB_ZCL_METERING_FORMATTING(5, 3, false),
		.metering_device_type = ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC,
	},
};

/* RMSVoltage in V, RMSCurrent in mA and ActivePower in W; outlet 2 as outlet 1 */
static struct zzb_zcl_electrical_measurement measurements[OUTLET_COUNT] = {
	{
		.measurement_type = ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_ACTIVE |
				    ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_PHASE_A,
		.ac_frequency = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
		.ac_frequency_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
		.ac_frequency_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
		.ac_frequency_multiplier = 1,
		.ac_frequency_divisor = 1,
		.rms_voltage = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
		.rms_voltage_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
		.rms_voltage_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
		.rms_current = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
		.rms_current_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
		.rms_current_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
		.active_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
		.active_power_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
		.active_power_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
		.reactive_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER_UNKNOWN,
		.apparent_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER_UNKNOWN,
		.ac_voltage_multiplier = 1,
		.ac_voltage_divisor = 1,
		.ac_current_multiplier = 1,
		.ac_current_divisor = 1000,
		.ac_power_multiplier = 1,
		.ac_power_divisor = 1,
	},
};

static const struct zzb_zcl_cluster outlet1_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify[0]),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes[0]),
	ZZB_ZCL_ON_OFF_SERVER(&outlets[0]),
	ZZB_ZCL_METERING_SERVER(&metering[0]),
	ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(&measurements[0]),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_TIME_CLIENT(),
};

static const struct zzb_zcl_cluster outlet2_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&identify[1]),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes[1]),
	ZZB_ZCL_ON_OFF_SERVER(&outlets[1]),
	ZZB_ZCL_METERING_SERVER(&metering[1]),
	ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(&measurements[1]),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

/* Two Smart Plugs */
static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_SMART_PLUG,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_SMART_PLUG),
		.clusters = outlet1_clusters,
		.cluster_count = ARRAY_SIZE(outlet1_clusters),
	},
	{
		.id = 2,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_SMART_PLUG,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_SMART_PLUG),
		.clusters = outlet2_clusters,
		.cluster_count = ARRAY_SIZE(outlet2_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* led0 is outlet 1, led1 outlet 2 */
static const struct gpio_dt_spec outlet2_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});

/* For the meter timer */
static struct zzb_node *strip_node;

/* OnOff of the outlets for the button and the timer, which run outside the stack thread */
static atomic_t outlet_on[OUTLET_COUNT];

static void attr_set(struct zzb_node *node, int i, uint16_t cluster, uint16_t attr,
		     const void *value)
{
	int err = zzb_zcl_attr_set(node, i + 1, cluster, attr, value);

	if (err) {
		LOG_ERR("outlet %d attribute 0x%04x of cluster 0x%04x %d", i + 1, attr, cluster,
			err);
	}
}

/* The measurements follow the outlet; RMSVoltage is that of the mains */
static void outlet_set(struct zzb_node *node, int i, bool on)
{
	uint16_t voltage = VOLTAGE;
	uint16_t current = on ? currents[i] : 0;
	int16_t power = on ? loads[i] : 0;
	int32_t demand = power;

	atomic_set(&outlet_on[i], on);
	if (i == 0) {
		sample_led(on);
	} else if (gpio_is_ready_dt(&outlet2_led)) {
		gpio_pin_set_dt(&outlet2_led, on);
	}

	LOG_INF("outlet %d %s", i + 1, on ? "on" : "off");
	attr_set(node, i, ZZB_ZCL_ELECTRICAL_MEASUREMENT,
		 ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE, &voltage);
	attr_set(node, i, ZZB_ZCL_ELECTRICAL_MEASUREMENT,
		 ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT, &current);
	attr_set(node, i, ZZB_ZCL_ELECTRICAL_MEASUREMENT,
		 ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER, &power);
	attr_set(node, i, ZZB_ZCL_METERING, ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND, &demand);
}

static void meter_tick(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(meter_work, meter_tick);

/* CurrentSummationDelivered counts Wh: the energy of each period while the outlet is on */
static void meter_tick(struct k_work *work)
{
	static uint64_t energy_ws[OUTLET_COUNT];
	uint64_t summation;

	k_work_schedule(&meter_work, K_SECONDS(METER_PERIOD_S));
	for (int i = 0; i < OUTLET_COUNT; i++) {
		if (!atomic_get(&outlet_on[i])) {
			continue;
		}

		energy_ws[i] += loads[i] * METER_PERIOD_S;
		summation = energy_ws[i] / SEC_PER_HOUR;
		attr_set(strip_node, i, ZZB_ZCL_METERING,
			 ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED, &summation);
	}
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(identify[0].identify_time > 0 || identify[1].identify_time > 0);
		return;
	}

	if (cluster == ZZB_ZCL_ON_OFF && attr == ZZB_ZCL_ON_OFF_ON_OFF) {
		outlet_set(node, ep - 1, outlets[ep - 1].on_off);
	}
}

/* Outlet 1 and outlet 2 in turn */
void sample_button(struct zzb_node *node)
{
	static int press;
	int i = press++ % OUTLET_COUNT;
	bool on = !atomic_get(&outlet_on[i]);
	int err = zzb_zcl_attr_set(node, i + 1, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff of outlet %d: %d", i + 1, err);
		return;
	}

	outlet_set(node, i, on);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, 1));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
	};
	int err;

	metering[1] = metering[0];
	measurements[1] = measurements[0];
	if (gpio_is_ready_dt(&outlet2_led)) {
		gpio_pin_configure_dt(&outlet2_led, GPIO_OUTPUT_INACTIVE);
	}

	err = sample_init(&cfg, &strip_node);
	if (err) {
		return err;
	}

	/* StartUpOnOff has set OnOff without zcl_cb */
	for (int i = 0; i < OUTLET_COUNT; i++) {
		outlet_set(strip_node, i, outlets[i].on_off);
	}

	k_work_schedule(&meter_work, K_SECONDS(METER_PERIOD_S));
	return zzb_bdb_start(strip_node);
}
