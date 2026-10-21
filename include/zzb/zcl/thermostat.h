/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Thermostat and Thermostat User Interface Configuration clusters.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_THERMOSTAT_H_
#define ZZB_INCLUDE_ZZB_ZCL_THERMOSTAT_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Thermostat cluster (ZCL 6.3). */
#define ZZB_ZCL_THERMOSTAT 0x0201

/** Attributes (ZCL Tables 6-11, 6-13 and 6-20). @{ */
#define ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE             0x0000
#define ZZB_ZCL_THERMOSTAT_OUTDOOR_TEMPERATURE           0x0001
#define ZZB_ZCL_THERMOSTAT_OCCUPANCY                     0x0002
#define ZZB_ZCL_THERMOSTAT_ABS_MIN_HEAT_SETPOINT_LIMIT   0x0003
#define ZZB_ZCL_THERMOSTAT_ABS_MAX_HEAT_SETPOINT_LIMIT   0x0004
#define ZZB_ZCL_THERMOSTAT_ABS_MIN_COOL_SETPOINT_LIMIT   0x0005
#define ZZB_ZCL_THERMOSTAT_ABS_MAX_COOL_SETPOINT_LIMIT   0x0006
#define ZZB_ZCL_THERMOSTAT_PI_COOLING_DEMAND             0x0007
#define ZZB_ZCL_THERMOSTAT_PI_HEATING_DEMAND             0x0008
#define ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION 0x0010
#define ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT     0x0011
#define ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT     0x0012
#define ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT   0x0013
#define ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT   0x0014
#define ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT       0x0015
#define ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT       0x0016
#define ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT       0x0017
#define ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT       0x0018
#define ZZB_ZCL_THERMOSTAT_MIN_SETPOINT_DEAD_BAND        0x0019
#define ZZB_ZCL_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION 0x001b
#define ZZB_ZCL_THERMOSTAT_SYSTEM_MODE                   0x001c
#define ZZB_ZCL_THERMOSTAT_ALARM_MASK                    0x001d
#define ZZB_ZCL_THERMOSTAT_RUNNING_MODE                  0x001e
#define ZZB_ZCL_THERMOSTAT_START_OF_WEEK                 0x0020
#define ZZB_ZCL_THERMOSTAT_NUMBER_OF_WEEKLY_TRANSITIONS  0x0021
#define ZZB_ZCL_THERMOSTAT_NUMBER_OF_DAILY_TRANSITIONS   0x0022
#define ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE    0x0025
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE                 0x0029
/** @} */

/** Commands received (ZCL Table 6-35). @{ */
#define ZZB_ZCL_THERMOSTAT_SETPOINT_RAISE_LOWER  0x00
#define ZZB_ZCL_THERMOSTAT_SET_WEEKLY_SCHEDULE   0x01
#define ZZB_ZCL_THERMOSTAT_GET_WEEKLY_SCHEDULE   0x02
#define ZZB_ZCL_THERMOSTAT_CLEAR_WEEKLY_SCHEDULE 0x03
/** @} */

/** Commands generated (ZCL Table 6-39). */
#define ZZB_ZCL_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP 0x00

/** Thermostat User Interface Configuration cluster (ZCL 6.6). */
#define ZZB_ZCL_THERMOSTAT_UI 0x0204

/** Attributes (ZCL Table 6-49). @{ */
#define ZZB_ZCL_THERMOSTAT_UI_TEMPERATURE_DISPLAY_MODE        0x0000
#define ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT                  0x0001
#define ZZB_ZCL_THERMOSTAT_UI_SCHEDULE_PROGRAMMING_VISIBILITY 0x0002
/** @} */

/** LocalTemperature and OutdoorTemperature of an invalid measurement (ZCL 6.3.2.2.1.1). @{ */
#define ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_UNKNOWN   INT16_MIN
#define ZZB_ZCL_THERMOSTAT_OUTDOOR_TEMPERATURE_UNKNOWN INT16_MIN
/** @} */

/** Bits of Occupancy (ZCL 6.3.2.2.1.3). */
#define ZZB_ZCL_THERMOSTAT_OCCUPANCY_OCCUPIED BIT(0)

/** Values of ControlSequenceOfOperation (ZCL Table 6-15). */
enum zzb_zcl_control_sequence {
	ZZB_ZCL_SEQUENCE_COOLING_ONLY = 0x00,
	ZZB_ZCL_SEQUENCE_COOLING_REHEAT = 0x01,
	ZZB_ZCL_SEQUENCE_HEATING_ONLY = 0x02,
	ZZB_ZCL_SEQUENCE_HEATING_REHEAT = 0x03,
	ZZB_ZCL_SEQUENCE_COOLING_HEATING = 0x04,
	ZZB_ZCL_SEQUENCE_COOLING_HEATING_REHEAT = 0x05,
};

/** Values of SystemMode (ZCL Table 6-16); 0x02 is reserved. */
enum zzb_zcl_system_mode {
	ZZB_ZCL_SYSTEM_MODE_OFF = 0x00,
	ZZB_ZCL_SYSTEM_MODE_AUTO = 0x01,
	ZZB_ZCL_SYSTEM_MODE_COOL = 0x03,
	ZZB_ZCL_SYSTEM_MODE_HEAT = 0x04,
	ZZB_ZCL_SYSTEM_MODE_EMERGENCY_HEATING = 0x05,
	ZZB_ZCL_SYSTEM_MODE_PRECOOLING = 0x06,
	ZZB_ZCL_SYSTEM_MODE_FAN_ONLY = 0x07,
	ZZB_ZCL_SYSTEM_MODE_DRY = 0x08,
	ZZB_ZCL_SYSTEM_MODE_SLEEP = 0x09,
};

/** Bits of AlarmMask; bit n enables alarm code n (ZCL Table 6-18). @{ */
#define ZZB_ZCL_THERMOSTAT_ALARM_MASK_INITIALIZATION_FAILURE   BIT(0)
#define ZZB_ZCL_THERMOSTAT_ALARM_MASK_HARDWARE_FAILURE         BIT(1)
#define ZZB_ZCL_THERMOSTAT_ALARM_MASK_SELF_CALIBRATION_FAILURE BIT(2)
/** @} */

/** Values of ThermostatRunningMode (ZCL Table 6-19). */
enum zzb_zcl_thermostat_running_mode {
	ZZB_ZCL_THERMOSTAT_RUNNING_MODE_OFF = 0x00,
	ZZB_ZCL_THERMOSTAT_RUNNING_MODE_COOL = 0x03,
	ZZB_ZCL_THERMOSTAT_RUNNING_MODE_HEAT = 0x04,
};

/** Values of StartOfWeek (ZCL Table 6-21). */
enum zzb_zcl_thermostat_start_of_week {
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_SUNDAY = 0x00,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_MONDAY = 0x01,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_TUESDAY = 0x02,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_WEDNESDAY = 0x03,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_THURSDAY = 0x04,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_FRIDAY = 0x05,
	ZZB_ZCL_THERMOSTAT_START_OF_WEEK_SATURDAY = 0x06,
};

/** Bits of ThermostatProgrammingOperationMode (ZCL Table 6-23). @{ */
#define ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE_SCHEDULE      BIT(0)
#define ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE_AUTO_RECOVERY BIT(1)
#define ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE_ECONOMY       BIT(2)
/** @} */

/** Bits of ThermostatRunningState (ZCL Table 6-24). @{ */
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_HEAT         BIT(0)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_COOL         BIT(1)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_FAN          BIT(2)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_HEAT_STAGE_2 BIT(3)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_COOL_STAGE_2 BIT(4)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_FAN_STAGE_2  BIT(5)
#define ZZB_ZCL_THERMOSTAT_RUNNING_STATE_FAN_STAGE_3  BIT(6)
/** @} */

/** Values of SetpointChangeSource (ZCL Table 6-26), which the server does not implement. */
enum zzb_zcl_thermostat_setpoint_change_source {
	ZZB_ZCL_THERMOSTAT_SETPOINT_CHANGE_SOURCE_MANUAL = 0x00,
	ZZB_ZCL_THERMOSTAT_SETPOINT_CHANGE_SOURCE_SCHEDULE = 0x01,
	ZZB_ZCL_THERMOSTAT_SETPOINT_CHANGE_SOURCE_EXTERNAL = 0x02,
};

/** Bits of Day of Week for Sequence of the weekly schedule (ZCL Table 6-37). @{ */
#define ZZB_ZCL_SCHEDULE_SUNDAY    BIT(0)
#define ZZB_ZCL_SCHEDULE_MONDAY    BIT(1)
#define ZZB_ZCL_SCHEDULE_TUESDAY   BIT(2)
#define ZZB_ZCL_SCHEDULE_WEDNESDAY BIT(3)
#define ZZB_ZCL_SCHEDULE_THURSDAY  BIT(4)
#define ZZB_ZCL_SCHEDULE_FRIDAY    BIT(5)
#define ZZB_ZCL_SCHEDULE_SATURDAY  BIT(6)
#define ZZB_ZCL_SCHEDULE_AWAY      BIT(7)
/** @} */

/** Mode bits of a sequence of the weekly schedule (ZCL Table 6-38). @{ */
#define ZZB_ZCL_SCHEDULE_HEAT BIT(0)
#define ZZB_ZCL_SCHEDULE_COOL BIT(1)
/** @} */

/** A transition of the weekly schedule (ZCL 6.3.2.3.2). */
struct zzb_zcl_schedule_transition {
	/** Minutes since midnight, 0 to 1439. */
	uint16_t time;
	/** Within their absolute limits, INT16_MIN when the mode of the day lacks it. */
	int16_t heating;
	int16_t cooling;
};

/** A day of the weekly schedule. */
struct zzb_zcl_schedule_day {
	/** ZZB_ZCL_SCHEDULE_HEAT and ZZB_ZCL_SCHEDULE_COOL, 0 for a day without transitions. */
	uint8_t mode;
	uint8_t count;
	/** count transitions in the order of the command, each at a different time. */
	struct zzb_zcl_schedule_transition transitions[CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS];
};

/**
 * Values of a Thermostat server (ZCL 6.3.2.2). Temperatures are in 0.01 degrees Celsius. The
 * application fills the fields up to system_mode_default before zzb_node_init() and changes them
 * with zzb_zcl_attr_set(); they are read-only. The stack keeps the other fields, the writable
 * attributes and the weekly schedule, and saves them.
 */
struct zzb_zcl_thermostat {
	/**
	 * With local_temperature_calibration applied by the application, or
	 * ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_UNKNOWN.
	 */
	int16_t local_temperature;
	/** ZZB_ZCL_THERMOSTAT_OUTDOOR_TEMPERATURE_UNKNOWN when unknown. */
	int16_t outdoor_temperature;
	/**
	 * ZZB_ZCL_THERMOSTAT_OCCUPANCY_* bits. The default of ZCL is occupied, which a thermostat
	 * without a sensor sets.
	 */
	uint8_t occupancy;
	/** Limits of the manufacturer; the defaults of ZCL are 700, 3000, 1600 and 3200. */
	int16_t abs_min_heat_setpoint_limit;
	int16_t abs_max_heat_setpoint_limit;
	int16_t abs_min_cool_setpoint_limit;
	int16_t abs_max_cool_setpoint_limit;
	/** Percent. */
	uint8_t pi_cooling_demand;
	uint8_t pi_heating_demand;
	/** 0.1 degrees Celsius, 0 to 25. */
	int8_t min_setpoint_dead_band;
	/** enum zzb_zcl_control_sequence. */
	uint8_t control_sequence_of_operation;
	/** ZZB_ZCL_THERMOSTAT_ALARM_MASK_* bits of the alarms that the application raises. */
	uint8_t alarm_mask;
	/** enum zzb_zcl_thermostat_running_mode. */
	uint8_t thermostat_running_mode;
	/** The first day of the weekly schedule, enum zzb_zcl_thermostat_start_of_week. */
	uint8_t start_of_week;
	/** ZZB_ZCL_THERMOSTAT_RUNNING_STATE_* bits. */
	uint16_t thermostat_running_state;
	/**
	 * Factory value of SystemMode, enum zzb_zcl_system_mode; 0 is Off. A node without a record
	 * and the reset of the node take it.
	 */
	uint8_t system_mode_default;
	/** 0.1 degrees Celsius, -25 to 25, applied by the application to its measurement. */
	int8_t local_temperature_calibration;
	/** Within the limits of their kind; by default 2600 and 2000 within the absolute limits. */
	int16_t occupied_cooling_setpoint;
	int16_t occupied_heating_setpoint;
	int16_t unoccupied_cooling_setpoint;
	int16_t unoccupied_heating_setpoint;
	/** Within the absolute limits of their kind, which are their defaults. */
	int16_t min_heat_setpoint_limit;
	int16_t max_heat_setpoint_limit;
	int16_t min_cool_setpoint_limit;
	int16_t max_cool_setpoint_limit;
	/** enum zzb_zcl_system_mode, within the modes of control_sequence_of_operation. */
	uint8_t system_mode;
	/**
	 * ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE_* bits; with SCHEDULE the application
	 * follows the weekly schedule.
	 */
	uint8_t thermostat_programming_operation_mode;
	/**
	 * Weekly schedule, element n for bit n of ZZB_ZCL_SCHEDULE_SUNDAY to ZZB_ZCL_SCHEDULE_AWAY:
	 * 0 Sunday to 6 Saturday, 7 Away or Vacation. The stack has no clock: the application reads
	 * the schedule on the stack thread and applies its setpoints with zzb_zcl_attr_set().
	 */
	struct zzb_zcl_schedule_day schedule[8];
};

/** Values of TemperatureDisplayMode (ZCL Table 6-50). */
enum zzb_zcl_thermostat_ui_temperature_display_mode {
	ZZB_ZCL_THERMOSTAT_UI_TEMPERATURE_DISPLAY_MODE_CELSIUS = 0x00,
	ZZB_ZCL_THERMOSTAT_UI_TEMPERATURE_DISPLAY_MODE_FAHRENHEIT = 0x01,
};

/** Values of KeypadLockout (ZCL Table 6-51). */
enum zzb_zcl_thermostat_ui_keypad_lockout {
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_NONE = 0x00,
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_LEVEL_1 = 0x01,
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_LEVEL_2 = 0x02,
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_LEVEL_3 = 0x03,
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_LEVEL_4 = 0x04,
	ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT_LEVEL_5 = 0x05,
};

/** Values of ScheduleProgrammingVisibility (ZCL Table 6-52). */
enum zzb_zcl_thermostat_ui_schedule_programming_visibility {
	ZZB_ZCL_THERMOSTAT_UI_SCHEDULE_PROGRAMMING_VISIBILITY_ENABLED = 0x00,
	ZZB_ZCL_THERMOSTAT_UI_SCHEDULE_PROGRAMMING_VISIBILITY_DISABLED = 0x01,
};

/**
 * Values of a Thermostat User Interface Configuration server (ZCL 6.6.2.1), writable and saved. The
 * application applies them to its display and keypad.
 */
struct zzb_zcl_thermostat_ui {
	/** enum zzb_zcl_thermostat_ui_temperature_display_mode. */
	uint8_t temperature_display_mode;
	/** enum zzb_zcl_thermostat_ui_keypad_lockout; the device defines the levels of lockout. */
	uint8_t keypad_lockout;
	/**
	 * enum zzb_zcl_thermostat_ui_schedule_programming_visibility; DISABLED hides the local
	 * programming of the weekly schedule.
	 */
	uint8_t schedule_programming_visibility;
};

/** Internal: referenced by the instance macros of the clusters. @{ */
extern const struct zzb_zcl_cluster_def zzb_zcl_thermostat_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_thermostat_client_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_thermostat_ui_def;
/** @} */

/** A Thermostat server with the values v, a pointer to struct zzb_zcl_thermostat. */
#define ZZB_ZCL_THERMOSTAT_SERVER(v)                                                               \
	{                                                                                          \
		.id = ZZB_ZCL_THERMOSTAT,                                                          \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_thermostat *)NULL),                           \
		.def = &zzb_zcl_thermostat_def,                                                    \
	}

/** A Thermostat client; it accepts reports and the responses of a Thermostat server. */
#define ZZB_ZCL_THERMOSTAT_CLIENT()                                                                \
	{                                                                                          \
		.id = ZZB_ZCL_THERMOSTAT,                                                          \
		.server = false,                                                                   \
		.def = &zzb_zcl_thermostat_client_def,                                             \
	}

/**
 * A Thermostat User Interface Configuration server with the values v, a pointer to struct
 * zzb_zcl_thermostat_ui.
 */
#define ZZB_ZCL_THERMOSTAT_UI_SERVER(v)                                                            \
	{                                                                                          \
		.id = ZZB_ZCL_THERMOSTAT_UI,                                                       \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_thermostat_ui *)NULL),                        \
		.def = &zzb_zcl_thermostat_ui_def,                                                 \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_THERMOSTAT_H_ */
