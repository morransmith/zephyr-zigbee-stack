/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_DEVICE_TEMP_H_
#define ZZB_INCLUDE_ZZB_ZCL_DEVICE_TEMP_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Device Temperature Configuration cluster (ZCL 3.4). */
#define ZZB_ZCL_DEVICE_TEMP 0x0002

/** Attributes (ZCL Tables 3-28 and 3-29). */
#define ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE        0x0000
#define ZZB_ZCL_DEVICE_TEMP_MIN_TEMP_EXPERIENCED       0x0001
#define ZZB_ZCL_DEVICE_TEMP_MAX_TEMP_EXPERIENCED       0x0002
#define ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL      0x0003
#define ZZB_ZCL_DEVICE_TEMP_ALARM_MASK                 0x0010
#define ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_THRESHOLD         0x0011
#define ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_THRESHOLD        0x0012
#define ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_DWELL_TRIP_POINT  0x0013
#define ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_DWELL_TRIP_POINT 0x0014

/**
 * Values of a Device Temperature Configuration server (ZCL 3.4.2.2). Temperatures are in degrees
 * Celsius, INT16_MIN (the non-value 0x8000) when unknown. The application sets
 * current_temperature and over_temp_total_dwell before zzb_node_init() and changes them with
 * zzb_zcl_attr_set(); they are read-only. The stack keeps the other fields; the alarm settings are
 * writable. The attributes other than current_temperature are saved.
 */
struct zzb_zcl_device_temp {
	int16_t current_temperature;
	/** Hours above HighTempThreshold; a saved value replaces it at zzb_node_init(). */
	uint16_t over_temp_total_dwell;
	/** Follow current_temperature. */
	int16_t min_temp_experienced;
	int16_t max_temp_experienced;
	uint8_t alarm_mask;
	int16_t low_temp_threshold;
	int16_t high_temp_threshold;
	/** Seconds, 0xffffff disables the alarm. */
	uint32_t low_temp_dwell_trip_point;
	uint32_t high_temp_dwell_trip_point;
	/** Internal: bit n while the condition of alarm n lasts. */
	uint8_t alarm_active;
	/** Internal: bit n once alarm n is raised during its condition. */
	uint8_t alarm_raised;
	/** Internal: k_uptime_get() at the start of the condition of alarm n. */
	int64_t alarm_since[2];
};

extern const struct zzb_zcl_cluster_def zzb_zcl_device_temp_def;

/**
 * A Device Temperature Configuration server with the values v, a pointer to struct
 * zzb_zcl_device_temp.
 */
#define ZZB_ZCL_DEVICE_TEMP_SERVER(v)                                                              \
	{                                                                                          \
		.id = ZZB_ZCL_DEVICE_TEMP,                                                         \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_device_temp *)NULL),                          \
		.def = &zzb_zcl_device_temp_def,                                                   \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_DEVICE_TEMP_H_ */
