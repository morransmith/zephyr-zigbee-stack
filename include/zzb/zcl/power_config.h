/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_
#define ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power Configuration cluster (ZCL 3.3). */
#define ZZB_ZCL_POWER_CONFIG 0x0001

/** Attributes of the mains and of the first battery source (ZCL 3.3.2.2). */
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE                    0x0000
#define ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY                  0x0001
#define ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK                 0x0010
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD      0x0011
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD      0x0012
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT   0x0013
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE                  0x0020
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING     0x0021
#define ZZB_ZCL_POWER_CONFIG_BATTERY_MANUFACTURER             0x0030
#define ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE                     0x0031
#define ZZB_ZCL_POWER_CONFIG_BATTERY_AHR_RATING               0x0032
#define ZZB_ZCL_POWER_CONFIG_BATTERY_QUANTITY                 0x0033
#define ZZB_ZCL_POWER_CONFIG_BATTERY_RATED_VOLTAGE            0x0034
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK               0x0035
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD    0x0036
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1       0x0037
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD2       0x0038
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD3       0x0039
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_MIN_THRESHOLD 0x003a
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD1    0x003b
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD2    0x003c
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3    0x003d
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE              0x003e

/**
 * Values of a Power Configuration server (ZCL 3.3.2.2). The application fills the fields up to
 * battery_rated_voltage before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only. The stack keeps the other fields; the alarm settings are writable and saved.
 */
struct zzb_zcl_power_config {
	/** Units of 100 mV, 0xffff unknown. */
	uint16_t mains_voltage;
	/** Half the frequency in Hz (ZCL 3.3.2.2.1.2). */
	uint8_t mains_frequency;
	/** Units of 100 mV, 0xff unknown. */
	uint8_t battery_voltage;
	/** Units of 0.5 %, 0xff unknown. */
	uint8_t battery_percentage_remaining;
	const char *battery_manufacturer;
	/** ZCL Table 3-23. */
	uint8_t battery_size;
	/** Units of 10 mAh. */
	uint16_t battery_ahr_rating;
	uint8_t battery_quantity;
	/** Units of 100 mV. */
	uint8_t battery_rated_voltage;
	uint8_t mains_alarm_mask;
	uint16_t mains_voltage_min_threshold;
	uint16_t mains_voltage_max_threshold;
	/** Seconds. */
	uint16_t mains_voltage_dwell_trip_point;
	uint8_t battery_alarm_mask;
	uint8_t battery_voltage_min_threshold;
	uint8_t battery_voltage_threshold1;
	uint8_t battery_voltage_threshold2;
	uint8_t battery_voltage_threshold3;
	/** Percent. */
	uint8_t battery_percentage_min_threshold;
	uint8_t battery_percentage_threshold1;
	uint8_t battery_percentage_threshold2;
	uint8_t battery_percentage_threshold3;
	/**
	 * ZCL Table 3-26. Bits 0 to 3 follow the battery alarms; the application may set the other
	 * bits with zzb_zcl_attr_set() and keeps bits 0 to 3 as they are.
	 */
	uint32_t battery_alarm_state;
	/** Internal: bit n while the condition of battery alarm 0x10 + n lasts. */
	uint8_t battery_active;
	/** Internal: bit n while the condition of mains voltage alarm n lasts. */
	uint8_t mains_active;
	/** Internal: bit n once mains voltage alarm n is raised during its condition. */
	uint8_t mains_raised;
	/** Internal: k_uptime_get() at the start of the condition of mains voltage alarm n. */
	int64_t mains_since[2];
};

extern const struct zzb_zcl_cluster_def zzb_zcl_power_config_def;

/** A Power Configuration server with the values v, a pointer to struct zzb_zcl_power_config. */
#define ZZB_ZCL_POWER_CONFIG_SERVER(v)                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_POWER_CONFIG,                                                        \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_power_config *)NULL),                         \
		.def = &zzb_zcl_power_config_def,                                                  \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_ */
