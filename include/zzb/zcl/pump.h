/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Pump Configuration and Control cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_PUMP_H_
#define ZZB_INCLUDE_ZZB_ZCL_PUMP_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pump Configuration and Control cluster (ZCL 6.2). */
#define ZZB_ZCL_PUMP 0x0200

/** Attributes (ZCL Tables 6-3, 6-4 and 6-6). */
#define ZZB_ZCL_PUMP_MAX_PRESSURE             0x0000
#define ZZB_ZCL_PUMP_MAX_SPEED                0x0001
#define ZZB_ZCL_PUMP_MAX_FLOW                 0x0002
#define ZZB_ZCL_PUMP_MIN_CONST_PRESSURE       0x0003
#define ZZB_ZCL_PUMP_MAX_CONST_PRESSURE       0x0004
#define ZZB_ZCL_PUMP_MIN_COMP_PRESSURE        0x0005
#define ZZB_ZCL_PUMP_MAX_COMP_PRESSURE        0x0006
#define ZZB_ZCL_PUMP_MIN_CONST_SPEED          0x0007
#define ZZB_ZCL_PUMP_MAX_CONST_SPEED          0x0008
#define ZZB_ZCL_PUMP_MIN_CONST_FLOW           0x0009
#define ZZB_ZCL_PUMP_MAX_CONST_FLOW           0x000a
#define ZZB_ZCL_PUMP_MIN_CONST_TEMP           0x000b
#define ZZB_ZCL_PUMP_MAX_CONST_TEMP           0x000c
#define ZZB_ZCL_PUMP_STATUS                   0x0010
#define ZZB_ZCL_PUMP_EFFECTIVE_OPERATION_MODE 0x0011
#define ZZB_ZCL_PUMP_EFFECTIVE_CONTROL_MODE   0x0012
#define ZZB_ZCL_PUMP_CAPACITY                 0x0013
#define ZZB_ZCL_PUMP_SPEED                    0x0014
#define ZZB_ZCL_PUMP_LIFETIME_RUNNING_HOURS   0x0015
#define ZZB_ZCL_PUMP_POWER                    0x0016
#define ZZB_ZCL_PUMP_LIFETIME_ENERGY_CONSUMED 0x0017
#define ZZB_ZCL_PUMP_OPERATION_MODE           0x0020
#define ZZB_ZCL_PUMP_CONTROL_MODE             0x0021
#define ZZB_ZCL_PUMP_ALARM_MASK               0x0022

/** Values that mark an attribute invalid or unknown (ZCL 6.2.2.2.1 and 6.2.2.2.2). */
#define ZZB_ZCL_PUMP_MAX_PRESSURE_UNKNOWN             INT16_MIN
#define ZZB_ZCL_PUMP_MAX_SPEED_UNKNOWN                0xffff
#define ZZB_ZCL_PUMP_MAX_FLOW_UNKNOWN                 0xffff
#define ZZB_ZCL_PUMP_MIN_CONST_PRESSURE_UNKNOWN       INT16_MIN
#define ZZB_ZCL_PUMP_MAX_CONST_PRESSURE_UNKNOWN       INT16_MIN
#define ZZB_ZCL_PUMP_MIN_COMP_PRESSURE_UNKNOWN        INT16_MIN
#define ZZB_ZCL_PUMP_MAX_COMP_PRESSURE_UNKNOWN        INT16_MIN
#define ZZB_ZCL_PUMP_MIN_CONST_SPEED_UNKNOWN          0xffff
#define ZZB_ZCL_PUMP_MAX_CONST_SPEED_UNKNOWN          0xffff
#define ZZB_ZCL_PUMP_MIN_CONST_FLOW_UNKNOWN           0xffff
#define ZZB_ZCL_PUMP_MAX_CONST_FLOW_UNKNOWN           0xffff
#define ZZB_ZCL_PUMP_MIN_CONST_TEMP_UNKNOWN           INT16_MIN
#define ZZB_ZCL_PUMP_MAX_CONST_TEMP_UNKNOWN           INT16_MIN
#define ZZB_ZCL_PUMP_CAPACITY_UNKNOWN                 INT16_MIN
#define ZZB_ZCL_PUMP_SPEED_UNKNOWN                    0xffff
#define ZZB_ZCL_PUMP_LIFETIME_RUNNING_HOURS_UNKNOWN   0xffffff
#define ZZB_ZCL_PUMP_POWER_UNKNOWN                    0xffffff
#define ZZB_ZCL_PUMP_LIFETIME_ENERGY_CONSUMED_UNKNOWN 0xffffffff

/** Bits of PumpStatus (ZCL Table 6-5). */
#define ZZB_ZCL_PUMP_STATUS_DEVICE_FAULT       BIT(0)
#define ZZB_ZCL_PUMP_STATUS_SUPPLY_FAULT       BIT(1)
#define ZZB_ZCL_PUMP_STATUS_SPEED_LOW          BIT(2)
#define ZZB_ZCL_PUMP_STATUS_SPEED_HIGH         BIT(3)
#define ZZB_ZCL_PUMP_STATUS_LOCAL_OVERRIDE     BIT(4)
#define ZZB_ZCL_PUMP_STATUS_RUNNING            BIT(5)
#define ZZB_ZCL_PUMP_STATUS_REMOTE_PRESSURE    BIT(6)
#define ZZB_ZCL_PUMP_STATUS_REMOTE_FLOW        BIT(7)
#define ZZB_ZCL_PUMP_STATUS_REMOTE_TEMPERATURE BIT(8)

/** Values of OperationMode and EffectiveOperationMode (ZCL Table 6-7). */
enum zzb_zcl_pump_operation_mode {
	ZZB_ZCL_PUMP_OPERATION_MODE_NORMAL = 0x00,
	ZZB_ZCL_PUMP_OPERATION_MODE_MINIMUM = 0x01,
	ZZB_ZCL_PUMP_OPERATION_MODE_MAXIMUM = 0x02,
	ZZB_ZCL_PUMP_OPERATION_MODE_LOCAL = 0x03,
};

/** Values of ControlMode and EffectiveControlMode (ZCL Table 6-8). */
enum zzb_zcl_pump_control_mode {
	ZZB_ZCL_PUMP_CONTROL_MODE_CONSTANT_SPEED = 0x00,
	ZZB_ZCL_PUMP_CONTROL_MODE_CONSTANT_PRESSURE = 0x01,
	ZZB_ZCL_PUMP_CONTROL_MODE_PROPORTIONAL_PRESSURE = 0x02,
	ZZB_ZCL_PUMP_CONTROL_MODE_CONSTANT_FLOW = 0x03,
	ZZB_ZCL_PUMP_CONTROL_MODE_CONSTANT_TEMPERATURE = 0x05,
	ZZB_ZCL_PUMP_CONTROL_MODE_AUTOMATIC = 0x07,
};

/** Bits of AlarmMask; bit n enables alarm code n (ZCL Table 6-9). */
#define ZZB_ZCL_PUMP_ALARM_MASK_SUPPLY_VOLTAGE_LOW           BIT(0)
#define ZZB_ZCL_PUMP_ALARM_MASK_SUPPLY_VOLTAGE_HIGH          BIT(1)
#define ZZB_ZCL_PUMP_ALARM_MASK_POWER_MISSING_PHASE          BIT(2)
#define ZZB_ZCL_PUMP_ALARM_MASK_SYSTEM_PRESSURE_LOW          BIT(3)
#define ZZB_ZCL_PUMP_ALARM_MASK_SYSTEM_PRESSURE_HIGH         BIT(4)
#define ZZB_ZCL_PUMP_ALARM_MASK_DRY_RUNNING                  BIT(5)
#define ZZB_ZCL_PUMP_ALARM_MASK_MOTOR_TEMPERATURE_HIGH       BIT(6)
#define ZZB_ZCL_PUMP_ALARM_MASK_MOTOR_FATAL_FAILURE          BIT(7)
#define ZZB_ZCL_PUMP_ALARM_MASK_ELECTRONIC_TEMPERATURE_HIGH  BIT(8)
#define ZZB_ZCL_PUMP_ALARM_MASK_BLOCKED                      BIT(9)
#define ZZB_ZCL_PUMP_ALARM_MASK_SENSOR_FAILURE               BIT(10)
#define ZZB_ZCL_PUMP_ALARM_MASK_ELECTRONIC_NON_FATAL_FAILURE BIT(11)
#define ZZB_ZCL_PUMP_ALARM_MASK_ELECTRONIC_FATAL_FAILURE     BIT(12)
#define ZZB_ZCL_PUMP_ALARM_MASK_GENERAL_FAULT                BIT(13)

/**
 * Values of a Pump Configuration and Control server (ZCL 6.2.2.2). lifetime_running_hours,
 * operation_mode and control_mode are writable and saved, with the default 0. The application sets
 * the other fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only. Pressures are in 0.1 kPa, speeds in RPM, flows in 0.1 m3/h and temperatures in 0.01
 * degrees Celsius; a value that is not available is the ZZB_ZCL_PUMP_*_UNKNOWN of its attribute.
 */
struct zzb_zcl_pump {
	int16_t max_pressure;
	uint16_t max_speed;
	uint16_t max_flow;
	int16_t min_const_pressure;
	int16_t max_const_pressure;
	int16_t min_comp_pressure;
	int16_t max_comp_pressure;
	uint16_t min_const_speed;
	uint16_t max_const_speed;
	uint16_t min_const_flow;
	uint16_t max_const_flow;
	int16_t min_const_temp;
	int16_t max_const_temp;
	/** ZZB_ZCL_PUMP_STATUS_* bits. */
	uint16_t pump_status;
	/** enum zzb_zcl_pump_operation_mode. */
	uint8_t effective_operation_mode;
	/** enum zzb_zcl_pump_control_mode. */
	uint8_t effective_control_mode;
	/** 0.005 percent of the effective maximum setpoint. */
	int16_t capacity;
	uint16_t speed;
	/** Hours, counted by the application with zzb_zcl_attr_set(). */
	uint32_t lifetime_running_hours;
	/** Watts. */
	uint32_t power;
	/** kWh; a saved value replaces it at zzb_node_init(). */
	uint32_t lifetime_energy_consumed;
	/** enum zzb_zcl_pump_operation_mode. */
	uint8_t operation_mode;
	/** enum zzb_zcl_pump_control_mode. */
	uint8_t control_mode;
	/** ZZB_ZCL_PUMP_ALARM_MASK_* bits. */
	uint16_t alarm_mask;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_pump_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_pump_client_def;

/**
 * A Pump Configuration and Control server with the values v, a pointer to struct zzb_zcl_pump.
 */
#define ZZB_ZCL_PUMP_SERVER(v)                                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_PUMP,                                                                \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_pump *)NULL),                                 \
		.def = &zzb_zcl_pump_def,                                                          \
	}

/** A Pump Configuration and Control client; it accepts reports. */
#define ZZB_ZCL_PUMP_CLIENT()                                                                      \
	{                                                                                          \
		.id = ZZB_ZCL_PUMP,                                                                \
		.server = false,                                                                   \
		.def = &zzb_zcl_pump_client_def,                                                   \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_PUMP_H_ */
