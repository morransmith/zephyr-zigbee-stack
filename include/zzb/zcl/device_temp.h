/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Device Temperature Configuration cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_DEVICE_TEMP_H_
#define ZZB_INCLUDE_ZZB_ZCL_DEVICE_TEMP_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Device Temperature Configuration cluster (ZCL 3.4). */
#define ZZB_ZCL_DEVICE_TEMP 0x0002

/** Attributes (ZCL Tables 3-28 and 3-29). @{ */
#define ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE        0x0000
#define ZZB_ZCL_DEVICE_TEMP_MIN_TEMP_EXPERIENCED       0x0001
#define ZZB_ZCL_DEVICE_TEMP_MAX_TEMP_EXPERIENCED       0x0002
#define ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL      0x0003
#define ZZB_ZCL_DEVICE_TEMP_ALARM_MASK                 0x0010
#define ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_THRESHOLD         0x0011
#define ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_THRESHOLD        0x0012
#define ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_DWELL_TRIP_POINT  0x0013
#define ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_DWELL_TRIP_POINT 0x0014
/** @} */

/** Unknown values (ZCL 3.4.2.2.1). @{ */
#define ZZB_ZCL_DEVICE_TEMP_UNKNOWN                       INT16_MIN
#define ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL_UNKNOWN 0xffff
/** @} */

/** Thresholds and dwell trip points that raise no alarm (ZCL 3.4.2.2.2). @{ */
#define ZZB_ZCL_DEVICE_TEMP_THRESHOLD_NONE        INT16_MIN
#define ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE 0xffffff
/** @} */

/** Bits of DeviceTempAlarmMask (ZCL Table 3-30). @{ */
#define ZZB_ZCL_DEVICE_TEMP_ALARM_MASK_TOO_LOW  BIT(0)
#define ZZB_ZCL_DEVICE_TEMP_ALARM_MASK_TOO_HIGH BIT(1)
/** @} */

/**
 * Values of a Device Temperature Configuration server (ZCL 3.4.2.2). Temperatures are in degrees
 * Celsius, ZZB_ZCL_DEVICE_TEMP_UNKNOWN when unknown. The application sets current_temperature and
 * over_temp_total_dwell before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only. The stack keeps the other fields; the alarm settings are writable. The attributes
 * other than current_temperature are saved.
 */
struct zzb_zcl_device_temp {
	int16_t current_temperature;
	/**
	 * Hours above HighTempThreshold, or ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL_UNKNOWN; a
	 * saved value replaces it at zzb_node_init().
	 */
	uint16_t over_temp_total_dwell;
	/** Follow current_temperature. */
	int16_t min_temp_experienced;
	int16_t max_temp_experienced;
	/** ZZB_ZCL_DEVICE_TEMP_ALARM_MASK_* bits. */
	uint8_t alarm_mask;
	/** ZZB_ZCL_DEVICE_TEMP_THRESHOLD_NONE raises no alarm. */
	int16_t low_temp_threshold;
	int16_t high_temp_threshold;
	/** Seconds, or ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE. */
	uint32_t low_temp_dwell_trip_point;
	uint32_t high_temp_dwell_trip_point;
	/** Internal: bit n while the condition of alarm n lasts. */
	uint8_t alarm_active;
	/** Internal: bit n once alarm n is raised during its condition. */
	uint8_t alarm_raised;
	/** Internal: k_uptime_get() at the start of the condition of alarm n. */
	int64_t alarm_since[2];
};

/** Internal: referenced by the instance macro of the cluster. */
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
