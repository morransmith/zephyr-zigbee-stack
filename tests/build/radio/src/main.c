/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/appliance_control.h>
#include <zzb/zcl/appliance_events.h>
#include <zzb/zcl/appliance_id.h>
#include <zzb/zcl/appliance_stats.h>
#include <zzb/zcl/ballast.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/binary_input.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/device_temp.h>
#include <zzb/zcl/door_lock.h>
#include <zzb/zcl/electrical_measurement.h>
#include <zzb/zcl/fan.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/ias_ace.h>
#include <zzb/zcl/ias_wd.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/meter_id.h>
#include <zzb/zcl/metering.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/on_off_switch_config.h>
#include <zzb/zcl/poll_control.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/power_profile.h>
#include <zzb/zcl/pump.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zcl/thermostat.h>
#include <zzb/zcl/time.h>
#include <zzb/zcl/window_covering.h>
#include <zzb/zzb.h>

/*
 * An Extended Color Light, a Color Dimmer Switch, a Dimmable Ballast, an Occupancy Sensor, a Window
 * Covering, a Window Covering Controller, a Thermostat, a Heating/Cooling Unit, a Pump, a Pump
 * Controller, an IAS Zone, an IAS Warning Device, an IAS ACE keypad, an IAS CIE, a Door Lock, a
 * Door Lock Controller, a Simple Sensor, a Smart Plug, a Meter Interface, a White Goods, a Home
 * Gateway and an On/Off Switch with On/Off Switch Configuration (Home Automation profile)
 */
#if defined(CONFIG_ZZB_ZCL)
static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "build",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;
static struct zzb_zcl_identify switch_identify;
static struct zzb_zcl_identify ballast_identify;
static struct zzb_zcl_identify sensor_identify;

static struct zzb_zcl_scenes scenes;
static struct zzb_zcl_scenes ballast_scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_on_off ballast_on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_level level = {
	.start_up_default = 0xff,
};

static struct zzb_zcl_level ballast_level = {
	.start_up_default = 0xff,
};

static struct zzb_zcl_color color = {
	.color_capabilities = ZZB_ZCL_COLOR_CAP_XY | ZZB_ZCL_COLOR_CAP_TEMPERATURE,
	.color_temp_physical_min = 153,
	.color_temp_physical_max = 500,
	.couple_color_temp_min = 153,
	.start_up_default = 0xffff,
};

static struct zzb_zcl_power_config power_config = {
	.mains_voltage = 2300,
	.mains_frequency = 100,
	.battery_voltage = 0xff,
	.battery_percentage_remaining = 0xff,
};

static struct zzb_zcl_device_temp device_temp = {
	.current_temperature = 25,
};

static struct zzb_zcl_ballast ballast = {
	.physical_min_level = 1,
	.physical_max_level = 254,
	.lamp_quantity = 1,
};

static struct zzb_zcl_alarms alarms;
static struct zzb_zcl_illuminance_level illuminance_level;
static struct zzb_zcl_illuminance illuminance;

static struct zzb_zcl_occupancy occupancy = {
	.occupancy_sensor_type_bitmap = 0x01,
};

static struct zzb_zcl_identify covering_identify;
static struct zzb_zcl_identify controller_identify;
static struct zzb_zcl_identify thermostat_identify;
static struct zzb_zcl_identify unit_identify;
static struct zzb_zcl_identify pump_identify;
static struct zzb_zcl_identify pump_controller_identify;
static struct zzb_zcl_identify zone_identify;
static struct zzb_zcl_identify wd_identify;
static struct zzb_zcl_identify keypad_identify;
static struct zzb_zcl_identify cie_identify;
static struct zzb_zcl_identify lock_identify;
static struct zzb_zcl_identify lock_controller_identify;
static struct zzb_zcl_identify input_identify;
static struct zzb_zcl_identify plug_identify;
static struct zzb_zcl_identify meter_identify;
static struct zzb_zcl_identify goods_identify;
static struct zzb_zcl_identify gateway_identify;
static struct zzb_zcl_identify on_off_switch_identify;
static struct zzb_zcl_scenes covering_scenes;
static struct zzb_zcl_scenes pump_scenes;
static struct zzb_zcl_scenes plug_scenes;

/* Tilt Blind - Lift and Tilt with closed loop on both axes */
static struct zzb_zcl_window_covering covering = {
	.window_covering_type = 0x08,
	.config_status = 0x1b,
	.current_position_lift_percentage = 0xff,
	.current_position_tilt_percentage = 0xff,
	.installed_closed_limit_lift = 200,
	.installed_closed_limit_tilt = 900,
};

static struct zzb_zcl_thermostat thermostat = {
	.local_temperature = INT16_MIN,
	.outdoor_temperature = INT16_MIN,
	.occupancy = 0x01,
	.abs_min_heat_setpoint_limit = 700,
	.abs_max_heat_setpoint_limit = 3000,
	.abs_min_cool_setpoint_limit = 1600,
	.abs_max_cool_setpoint_limit = 3200,
	.min_setpoint_dead_band = 25,
	.control_sequence_of_operation = ZZB_ZCL_SEQUENCE_COOLING_HEATING,
	.system_mode_default = ZZB_ZCL_SYSTEM_MODE_OFF,
};

static struct zzb_zcl_thermostat_ui thermostat_ui;

static struct zzb_zcl_fan fan = {
	.fan_mode_sequence = 0x02,
};

static struct zzb_zcl_fan unit_fan = {
	.fan_mode_sequence = 0x00,
};

static struct zzb_zcl_on_off unit_on_off;

static struct zzb_zcl_temperature temperature = {
	.measured_value = INT16_MIN,
	.min_measured_value = INT16_MIN,
	.max_measured_value = INT16_MIN,
};

static struct zzb_zcl_humidity humidity = {
	.measured_value = 0xffff,
	.min_measured_value = 0xffff,
	.max_measured_value = 0xffff,
};

static struct zzb_zcl_on_off pump_on_off;
static struct zzb_zcl_alarms pump_alarms;

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

static struct zzb_zcl_ias_zone contact = {
	.zone_type = 0x0015,
};

static struct zzb_zcl_poll_control contact_poll_control;

static struct zzb_zcl_ias_zone siren_zone = {
	.zone_type = 0x0225,
};

static struct zzb_zcl_ias_wd siren;

static struct zzb_zcl_ias_zone keypad_zone = {
	.zone_type = 0x021d,
};

static struct zzb_zcl_door_lock lock = {
	.lock_state = ZZB_ZCL_DOOR_LOCK_LOCKED,
	.actuator_enabled = true,
	.min_pin_code_length = 4,
	.max_pin_code_length = 8,
};

static struct zzb_zcl_alarms lock_alarms;

static struct zzb_zcl_binary_input input = {
	.description = "Contact",
};

static struct zzb_zcl_on_off plug_on_off;

/* kWh and kW with three decimals */
static struct zzb_zcl_metering plug_metering = {
	.multiplier = 1,
	.divisor = 1000,
	.summation_formatting = 0x2b,
	.demand_formatting = 0x2b,
};

static struct zzb_zcl_electrical_measurement plug_power = {
	.measurement_type = 0x09,
	.ac_frequency = 0xffff,
	.ac_frequency_min = 0xffff,
	.ac_frequency_max = 0xffff,
	.ac_frequency_multiplier = 1,
	.ac_frequency_divisor = 1,
	.rms_voltage = 0xffff,
	.rms_voltage_min = 0xffff,
	.rms_voltage_max = 0xffff,
	.rms_current = 0xffff,
	.rms_current_min = 0xffff,
	.rms_current_max = 0xffff,
	.active_power = INT16_MIN,
	.active_power_min = INT16_MIN,
	.active_power_max = INT16_MIN,
	.reactive_power = INT16_MIN,
	.apparent_power = 0xffff,
	.ac_voltage_multiplier = 1,
	.ac_voltage_divisor = 1,
	.ac_current_multiplier = 1,
	.ac_current_divisor = 1,
	.ac_power_multiplier = 1,
	.ac_power_divisor = 1,
};

/* A gas meter in m3 */
static struct zzb_zcl_metering gas_metering = {
	.unit_of_measure = 0x01,
	.multiplier = 1,
	.divisor = 1000,
	.metering_device_type = 0x01,
};

static struct zzb_zcl_meter_id meter_id = {
	.company_name = "zzb",
	.pod = "0",
};

static struct zzb_zcl_power_profile power_profile = {
	.total_profile_num = 1,
	.energy_formatting = 0x01,
};

static struct zzb_zcl_appliance_control appliance_control;
static struct zzb_zcl_appliance_id appliance_id;

static struct zzb_zcl_appliance_stats appliance_stats = {
	.log_max_size = 0x3c,
	.log_queue_max_size = 1,
};

static struct zzb_zcl_time gateway_time;
static struct zzb_zcl_on_off_switch_config switch_config;

static struct zzb_zcl_flow flow = {
	.measured_value = 0xffff,
	.min_measured_value = 0xffff,
	.max_measured_value = 0xffff,
};

static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_LEVEL_SERVER(&level),
	ZZB_ZCL_COLOR_SERVER(&color),
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&switch_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
	ZZB_ZCL_LEVEL_CLIENT(),
	ZZB_ZCL_COLOR_CLIENT(),
	ZZB_ZCL_SCENES_CLIENT(),
	ZZB_ZCL_GROUPS_CLIENT(),
};

static const struct zzb_zcl_cluster ballast_clusters[] = {
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_DEVICE_TEMP_SERVER(&device_temp),
	ZZB_ZCL_IDENTIFY_SERVER(&ballast_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&ballast_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&ballast_on_off),
	ZZB_ZCL_LEVEL_SERVER(&ballast_level),
	ZZB_ZCL_ALARMS_SERVER(&alarms),
	ZZB_ZCL_BALLAST_SERVER(&ballast),
	ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&illuminance_level),
	ZZB_ZCL_ILLUMINANCE_CLIENT(),
	ZZB_ZCL_OCCUPANCY_CLIENT(),
};

static const struct zzb_zcl_cluster sensor[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&sensor_identify),
	ZZB_ZCL_ILLUMINANCE_SERVER(&illuminance),
	ZZB_ZCL_OCCUPANCY_SERVER(&occupancy),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster covering_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&covering_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&covering_scenes),
	ZZB_ZCL_WINDOW_COVERING_SERVER(&covering),
};

static const struct zzb_zcl_cluster controller_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&controller_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_GROUPS_CLIENT(),
	ZZB_ZCL_SCENES_CLIENT(),
	ZZB_ZCL_WINDOW_COVERING_CLIENT(),
};

static const struct zzb_zcl_cluster thermostat_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&thermostat_identify),
	ZZB_ZCL_THERMOSTAT_SERVER(&thermostat),
	ZZB_ZCL_FAN_SERVER(&fan),
	ZZB_ZCL_THERMOSTAT_UI_SERVER(&thermostat_ui),
	ZZB_ZCL_TEMPERATURE_SERVER(&temperature),
	ZZB_ZCL_HUMIDITY_SERVER(&humidity),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_FAN_CLIENT(),
	ZZB_ZCL_HUMIDITY_CLIENT(),
};

static const struct zzb_zcl_cluster unit_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&unit_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_ON_OFF_SERVER(&unit_on_off),
	ZZB_ZCL_FAN_SERVER(&unit_fan),
	ZZB_ZCL_THERMOSTAT_CLIENT(),
};

static const struct zzb_zcl_cluster pump_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&pump_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&pump_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&pump_on_off),
	ZZB_ZCL_ALARMS_SERVER(&pump_alarms),
	ZZB_ZCL_PUMP_SERVER(&pump),
	ZZB_ZCL_PRESSURE_SERVER(&pressure),
	ZZB_ZCL_FLOW_SERVER(&flow),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster pump_controller_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&pump_controller_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
	ZZB_ZCL_PUMP_CLIENT(),
	ZZB_ZCL_TEMPERATURE_CLIENT(),
	ZZB_ZCL_PRESSURE_CLIENT(),
	ZZB_ZCL_FLOW_CLIENT(),
};

static const struct zzb_zcl_cluster zone_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&zone_identify),
	ZZB_ZCL_POLL_CONTROL_SERVER(&contact_poll_control),
	ZZB_ZCL_IAS_ZONE_SERVER(&contact),
};

static const struct zzb_zcl_cluster wd_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&wd_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_IAS_ZONE_SERVER(&siren_zone),
	ZZB_ZCL_IAS_WD_SERVER(&siren),
};

static const struct zzb_zcl_cluster keypad_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&keypad_identify),
	ZZB_ZCL_IAS_ZONE_SERVER(&keypad_zone),
	ZZB_ZCL_IAS_ACE_CLIENT(),
};

static const struct zzb_zcl_cluster cie_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&cie_identify),
	ZZB_ZCL_IAS_ACE_SERVER(),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_POLL_CONTROL_CLIENT(),
	ZZB_ZCL_IAS_ZONE_CLIENT(),
	ZZB_ZCL_IAS_WD_CLIENT(),
};

static const struct zzb_zcl_cluster lock_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&lock_identify),
	ZZB_ZCL_ALARMS_SERVER(&lock_alarms),
	ZZB_ZCL_DOOR_LOCK_SERVER(&lock),
};

static const struct zzb_zcl_cluster lock_controller_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&lock_controller_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_DOOR_LOCK_CLIENT(),
};

static const struct zzb_zcl_cluster input_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&input_identify),
	ZZB_ZCL_BINARY_INPUT_SERVER(&input),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster plug_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&plug_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&plug_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&plug_on_off),
	ZZB_ZCL_METERING_SERVER(&plug_metering),
	ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(&plug_power),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_TIME_CLIENT(),
};

static const struct zzb_zcl_cluster meter_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&meter_identify),
	ZZB_ZCL_METERING_SERVER(&gas_metering),
	ZZB_ZCL_METER_ID_SERVER(&meter_id),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster goods_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&goods_identify),
	ZZB_ZCL_POWER_PROFILE_SERVER(&power_profile),
	ZZB_ZCL_APPLIANCE_CONTROL_SERVER(&appliance_control),
	ZZB_ZCL_APPLIANCE_ID_SERVER(&appliance_id),
	ZZB_ZCL_APPLIANCE_EVENTS_SERVER(),
	ZZB_ZCL_APPLIANCE_STATS_SERVER(&appliance_stats),
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster gateway_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&gateway_identify),
	ZZB_ZCL_TIME_SERVER(&gateway_time),
	ZZB_ZCL_POWER_PROFILE_CLIENT(),
	ZZB_ZCL_METERING_CLIENT(),
	ZZB_ZCL_METER_ID_CLIENT(),
	ZZB_ZCL_ELECTRICAL_MEASUREMENT_CLIENT(),
	ZZB_ZCL_APPLIANCE_CONTROL_CLIENT(),
	ZZB_ZCL_APPLIANCE_ID_CLIENT(),
	ZZB_ZCL_APPLIANCE_EVENTS_CLIENT(),
	ZZB_ZCL_APPLIANCE_STATS_CLIENT(),
};

static const struct zzb_zcl_cluster on_off_switch_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&on_off_switch_identify),
	ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SERVER(&switch_config),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
};
#else
/* native_sim builds no ZCL, so the definitions of the stack clusters do not exist */
static const struct zzb_zcl_cluster light[] = {
	{.id = ZZB_ZCL_BASIC, .server = true},
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_LEVEL, .server = true},
	{.id = ZZB_ZCL_COLOR, .server = true},
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_ON_OFF},
	{.id = ZZB_ZCL_LEVEL},
	{.id = ZZB_ZCL_COLOR},
	{.id = ZZB_ZCL_SCENES},
	{.id = ZZB_ZCL_GROUPS},
};

static const struct zzb_zcl_cluster ballast_clusters[] = {
	{.id = ZZB_ZCL_POWER_CONFIG, .server = true},
	{.id = ZZB_ZCL_DEVICE_TEMP, .server = true},
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_LEVEL, .server = true},
	{.id = ZZB_ZCL_ALARMS, .server = true},
	{.id = ZZB_ZCL_BALLAST, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE_LEVEL, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE},
	{.id = ZZB_ZCL_OCCUPANCY},
};

static const struct zzb_zcl_cluster sensor[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_ILLUMINANCE, .server = true},
	{.id = ZZB_ZCL_OCCUPANCY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};

static const struct zzb_zcl_cluster covering_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_WINDOW_COVERING, .server = true},
};

static const struct zzb_zcl_cluster controller_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_GROUPS},
	{.id = ZZB_ZCL_SCENES},
	{.id = ZZB_ZCL_WINDOW_COVERING},
};

static const struct zzb_zcl_cluster thermostat_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_THERMOSTAT, .server = true},
	{.id = ZZB_ZCL_FAN, .server = true},
	{.id = ZZB_ZCL_THERMOSTAT_UI, .server = true},
	{.id = ZZB_ZCL_TEMPERATURE, .server = true},
	{.id = ZZB_ZCL_HUMIDITY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_FAN},
	{.id = ZZB_ZCL_HUMIDITY},
};

static const struct zzb_zcl_cluster unit_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_FAN, .server = true},
	{.id = ZZB_ZCL_THERMOSTAT},
};

static const struct zzb_zcl_cluster pump_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_ALARMS, .server = true},
	{.id = ZZB_ZCL_PUMP, .server = true},
	{.id = ZZB_ZCL_PRESSURE, .server = true},
	{.id = ZZB_ZCL_FLOW, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};

static const struct zzb_zcl_cluster pump_controller_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_ON_OFF},
	{.id = ZZB_ZCL_PUMP},
	{.id = ZZB_ZCL_TEMPERATURE},
	{.id = ZZB_ZCL_PRESSURE},
	{.id = ZZB_ZCL_FLOW},
};

static const struct zzb_zcl_cluster zone_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_POLL_CONTROL, .server = true},
	{.id = ZZB_ZCL_IAS_ZONE, .server = true},
};

static const struct zzb_zcl_cluster wd_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_IAS_ZONE, .server = true},
	{.id = ZZB_ZCL_IAS_WD, .server = true},
};

static const struct zzb_zcl_cluster keypad_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IAS_ZONE, .server = true},
	{.id = ZZB_ZCL_IAS_ACE},
};

static const struct zzb_zcl_cluster cie_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IAS_ACE, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_POLL_CONTROL},
	{.id = ZZB_ZCL_IAS_ZONE},
	{.id = ZZB_ZCL_IAS_WD},
};

static const struct zzb_zcl_cluster lock_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_ALARMS, .server = true},
	{.id = ZZB_ZCL_DOOR_LOCK, .server = true},
};

static const struct zzb_zcl_cluster lock_controller_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_DOOR_LOCK},
};

static const struct zzb_zcl_cluster input_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_BINARY_INPUT, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};

static const struct zzb_zcl_cluster plug_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
	{.id = ZZB_ZCL_METERING, .server = true},
	{.id = ZZB_ZCL_ELECTRICAL_MEASUREMENT, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_TIME},
};

static const struct zzb_zcl_cluster meter_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_METERING, .server = true},
	{.id = ZZB_ZCL_METER_ID, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};

static const struct zzb_zcl_cluster goods_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_POWER_PROFILE, .server = true},
	{.id = ZZB_ZCL_APPLIANCE_CONTROL, .server = true},
	{.id = ZZB_ZCL_APPLIANCE_ID, .server = true},
	{.id = ZZB_ZCL_APPLIANCE_EVENTS, .server = true},
	{.id = ZZB_ZCL_APPLIANCE_STATS, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
};

static const struct zzb_zcl_cluster gateway_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_TIME, .server = true},
	{.id = ZZB_ZCL_POWER_PROFILE},
	{.id = ZZB_ZCL_METERING},
	{.id = ZZB_ZCL_METER_ID},
	{.id = ZZB_ZCL_ELECTRICAL_MEASUREMENT},
	{.id = ZZB_ZCL_APPLIANCE_CONTROL},
	{.id = ZZB_ZCL_APPLIANCE_ID},
	{.id = ZZB_ZCL_APPLIANCE_EVENTS},
	{.id = ZZB_ZCL_APPLIANCE_STATS},
};

static const struct zzb_zcl_cluster on_off_switch_clusters[] = {
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_ON_OFF_SWITCH_CONFIG, .server = true},
	{.id = ZZB_ZCL_IDENTIFY},
	{.id = ZZB_ZCL_ON_OFF},
};
#endif

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x010d,
		.device_version = 2,
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0105,
		.device_version = 1,
		.clusters = switch_clusters,
		.cluster_count = ARRAY_SIZE(switch_clusters),
	},
	{
		.id = 3,
		.profile = 0x0104,
		.device_id = 0x0109,
		.device_version = 1,
		.clusters = ballast_clusters,
		.cluster_count = ARRAY_SIZE(ballast_clusters),
	},
	{
		.id = 4,
		.profile = 0x0104,
		.device_id = 0x0107,
		.device_version = 1,
		.clusters = sensor,
		.cluster_count = ARRAY_SIZE(sensor),
	},
	{
		.id = 5,
		.profile = 0x0104,
		.device_id = 0x0202,
		.device_version = 1,
		.clusters = covering_clusters,
		.cluster_count = ARRAY_SIZE(covering_clusters),
	},
	{
		.id = 6,
		.profile = 0x0104,
		.device_id = 0x0203,
		.device_version = 1,
		.clusters = controller_clusters,
		.cluster_count = ARRAY_SIZE(controller_clusters),
	},
	{
		.id = 7,
		.profile = 0x0104,
		.device_id = 0x0301,
		.device_version = 1,
		.clusters = thermostat_clusters,
		.cluster_count = ARRAY_SIZE(thermostat_clusters),
	},
	{
		.id = 8,
		.profile = 0x0104,
		.device_id = 0x0300,
		.device_version = 1,
		.clusters = unit_clusters,
		.cluster_count = ARRAY_SIZE(unit_clusters),
	},
	{
		.id = 9,
		.profile = 0x0104,
		.device_id = 0x0303,
		.device_version = 1,
		.clusters = pump_clusters,
		.cluster_count = ARRAY_SIZE(pump_clusters),
	},
	{
		.id = 10,
		.profile = 0x0104,
		.device_id = 0x0304,
		.device_version = 1,
		.clusters = pump_controller_clusters,
		.cluster_count = ARRAY_SIZE(pump_controller_clusters),
	},
	{
		.id = 11,
		.profile = 0x0104,
		.device_id = 0x0402,
		.device_version = 1,
		.clusters = zone_clusters,
		.cluster_count = ARRAY_SIZE(zone_clusters),
	},
	{
		.id = 12,
		.profile = 0x0104,
		.device_id = 0x0403,
		.device_version = 1,
		.clusters = wd_clusters,
		.cluster_count = ARRAY_SIZE(wd_clusters),
	},
	{
		.id = 13,
		.profile = 0x0104,
		.device_id = 0x0401,
		.device_version = 1,
		.clusters = keypad_clusters,
		.cluster_count = ARRAY_SIZE(keypad_clusters),
	},
	{
		.id = 14,
		.profile = 0x0104,
		.device_id = 0x0400,
		.device_version = 1,
		.clusters = cie_clusters,
		.cluster_count = ARRAY_SIZE(cie_clusters),
	},
	{
		.id = 15,
		.profile = 0x0104,
		.device_id = 0x000a,
		.device_version = 1,
		.clusters = lock_clusters,
		.cluster_count = ARRAY_SIZE(lock_clusters),
	},
	{
		.id = 16,
		.profile = 0x0104,
		.device_id = 0x000b,
		.device_version = 1,
		.clusters = lock_controller_clusters,
		.cluster_count = ARRAY_SIZE(lock_controller_clusters),
	},
	{
		.id = 17,
		.profile = 0x0104,
		.device_id = 0x000c,
		.device_version = 1,
		.clusters = input_clusters,
		.cluster_count = ARRAY_SIZE(input_clusters),
	},
	{
		.id = 18,
		.profile = 0x0104,
		.device_id = 0x0051,
		.device_version = 1,
		.clusters = plug_clusters,
		.cluster_count = ARRAY_SIZE(plug_clusters),
	},
	{
		.id = 19,
		.profile = 0x0104,
		.device_id = 0x0053,
		.device_version = 1,
		.clusters = meter_clusters,
		.cluster_count = ARRAY_SIZE(meter_clusters),
	},
	{
		.id = 20,
		.profile = 0x0104,
		.device_id = 0x0052,
		.device_version = 1,
		.clusters = goods_clusters,
		.cluster_count = ARRAY_SIZE(goods_clusters),
	},
	{
		.id = 21,
		.profile = 0x0104,
		.device_id = 0x0050,
		.device_version = 1,
		.clusters = gateway_clusters,
		.cluster_count = ARRAY_SIZE(gateway_clusters),
	},
	{
		.id = 22,
		.profile = 0x0104,
		.device_id = 0x0000,
		.device_version = 1,
		.clusters = on_off_switch_clusters,
		.cluster_count = ARRAY_SIZE(on_off_switch_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The switch toggles its bound lights after a join */
static void bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
#if defined(CONFIG_ZZB_ZCL)
	if (event == ZZB_BDB_EVENT_STEERING && status == ZZB_BDB_SUCCESS) {
		zzb_zcl_send_bound(node, 2, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_TOGGLE, NULL, 0);
	}
#endif
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
}

static void zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		       uint8_t variant)
{
}

/* The window covering has no motor, the warning device no siren and the CIE no panel here */
static uint8_t zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return ZZB_ZCL_SUCCESS;
}

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_effect_cb = zcl_effect,
		.zcl_cmd_cb = zcl_cmd,
	};
	struct zzb_node *node;
	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	int err = zzb_node_init(&cfg, &node);

	if (err || !IS_ENABLED(CONFIG_ZZB_BDB)) {
		return err;
	}

	return zzb_bdb_start(node);
}
