/* SPDX-License-Identifier: Apache-2.0 */

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

/**
 * Values of a Pump Configuration and Control server (ZCL 6.2.2.2). lifetime_running_hours,
 * operation_mode and control_mode are writable and saved, with the default 0. The application sets
 * the other fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only. Pressures are in 0.1 kPa, speeds in RPM, flows in 0.1 m3/h and temperatures in 0.01
 * degrees Celsius; INT16_MIN (0x8000) and 0xffff are invalid.
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
	/** ZCL Table 6-5. */
	uint16_t pump_status;
	/** ZCL Tables 6-7 and 6-8. */
	uint8_t effective_operation_mode;
	uint8_t effective_control_mode;
	/** 0.005 percent of the effective maximum setpoint. */
	int16_t capacity;
	uint16_t speed;
	/** Hours, counted by the application with zzb_zcl_attr_set(); 0xffffff unknown. */
	uint32_t lifetime_running_hours;
	/** Watts, 0xffffff unknown. */
	uint32_t power;
	/** kWh, 0xffffffff unknown; a saved value replaces it at zzb_node_init(). */
	uint32_t lifetime_energy_consumed;
	/** ZCL Table 6-7. */
	uint8_t operation_mode;
	/** ZCL Table 6-8. */
	uint8_t control_mode;
	/** Bit n enables alarm code n of ZCL Table 6-9. */
	uint16_t alarm_mask;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_pump_def;

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

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_PUMP_H_ */
