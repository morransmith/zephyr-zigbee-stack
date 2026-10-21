/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Illuminance Measurement and Illuminance Level Sensing clusters.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_ILLUMINANCE_H_
#define ZZB_INCLUDE_ZZB_ZCL_ILLUMINANCE_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Illuminance Measurement cluster (ZCL 4.2). */
#define ZZB_ZCL_ILLUMINANCE 0x0400

/** Attributes (ZCL Table 4-5). @{ */
#define ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE     0x0000
#define ZZB_ZCL_ILLUMINANCE_MIN_MEASURED_VALUE 0x0001
#define ZZB_ZCL_ILLUMINANCE_MAX_MEASURED_VALUE 0x0002
#define ZZB_ZCL_ILLUMINANCE_TOLERANCE          0x0003
#define ZZB_ZCL_ILLUMINANCE_LIGHT_SENSOR_TYPE  0x0004
/** @} */

/** MeasuredValue too low to be measured and MeasuredValue that is not valid (ZCL 4.2.2.2.1). @{ */
#define ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE_TOO_LOW 0x0000
#define ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE_UNKNOWN 0xffff
/** @} */

/** MinMeasuredValue and MaxMeasuredValue that are not defined (ZCL 4.2.2.2.2, 4.2.2.2.3). @{ */
#define ZZB_ZCL_ILLUMINANCE_MIN_MEASURED_VALUE_UNDEFINED 0xffff
#define ZZB_ZCL_ILLUMINANCE_MAX_MEASURED_VALUE_UNDEFINED 0xffff
/** @} */

/** LightSensorType of both clusters (ZCL Tables 4-6 and 4-10). */
enum zzb_zcl_illuminance_light_sensor_type {
	ZZB_ZCL_ILLUMINANCE_LIGHT_SENSOR_TYPE_PHOTODIODE = 0x00,
	ZZB_ZCL_ILLUMINANCE_LIGHT_SENSOR_TYPE_CMOS = 0x01,
	ZZB_ZCL_ILLUMINANCE_LIGHT_SENSOR_TYPE_UNKNOWN = 0xff,
};

/** Illuminance Level Sensing cluster (ZCL 4.3). */
#define ZZB_ZCL_ILLUMINANCE_LEVEL 0x0401

/** Attributes (ZCL Tables 4-8 and 4-11). @{ */
#define ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS            0x0000
#define ZZB_ZCL_ILLUMINANCE_LEVEL_LIGHT_SENSOR_TYPE 0x0001
#define ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL      0x0010
/** @} */

/** LevelStatus (ZCL Table 4-9). */
enum zzb_zcl_illuminance_level_status {
	ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_ON_TARGET = 0x00,
	ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_BELOW_TARGET = 0x01,
	ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS_ABOVE_TARGET = 0x02,
};

/** IlluminanceTargetLevel that is not valid (ZCL 4.3.2.4.1). */
#define ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL_UNKNOWN 0xffff

/**
 * Values of an Illuminance Measurement server (ZCL 4.2.2.2), all read-only and set by the
 * application.
 */
struct zzb_zcl_illuminance {
	/**
	 * 10000 log10(lux) + 1, or ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE_TOO_LOW or
	 * ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE_UNKNOWN.
	 */
	uint16_t measured_value;
	/** As measured_value, or ZZB_ZCL_ILLUMINANCE_MIN_MEASURED_VALUE_UNDEFINED. */
	uint16_t min_measured_value;
	/** As measured_value, or ZZB_ZCL_ILLUMINANCE_MAX_MEASURED_VALUE_UNDEFINED. */
	uint16_t max_measured_value;
	uint16_t tolerance;
	/** enum zzb_zcl_illuminance_light_sensor_type. */
	uint8_t light_sensor_type;
};

/**
 * Values of an Illuminance Level Sensing server (ZCL 4.3.2.3, 4.3.2.4). The application sets
 * level_status and light_sensor_type, which are read-only; illuminance_target_level is writable and
 * saved.
 */
struct zzb_zcl_illuminance_level {
	/**
	 * enum zzb_zcl_illuminance_level_status, computed by the application with its dead band
	 * around the target.
	 */
	uint8_t level_status;
	/** enum zzb_zcl_illuminance_light_sensor_type. */
	uint8_t light_sensor_type;
	/** 10000 log10(lux), or ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL_UNKNOWN. */
	uint16_t illuminance_target_level;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_illuminance_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_illuminance_level_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_illuminance_level_client_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_illuminance_client_def;

/** An Illuminance Measurement server with the values v, a pointer to struct zzb_zcl_illuminance. */
#define ZZB_ZCL_ILLUMINANCE_SERVER(v)                                                              \
	{                                                                                          \
		.id = ZZB_ZCL_ILLUMINANCE,                                                         \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_illuminance *)NULL),                          \
		.def = &zzb_zcl_illuminance_def,                                                   \
	}

/**
 * An Illuminance Level Sensing server with the values v, a pointer to struct
 * zzb_zcl_illuminance_level.
 */
#define ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(v)                                                        \
	{                                                                                          \
		.id = ZZB_ZCL_ILLUMINANCE_LEVEL,                                                   \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_illuminance_level *)NULL),                    \
		.def = &zzb_zcl_illuminance_level_def,                                             \
	}

/** An Illuminance Measurement client; it accepts reports. */
#define ZZB_ZCL_ILLUMINANCE_CLIENT()                                                               \
	{                                                                                          \
		.id = ZZB_ZCL_ILLUMINANCE,                                                         \
		.server = false,                                                                   \
		.def = &zzb_zcl_illuminance_client_def,                                            \
	}

/** An Illuminance Level Sensing client; it accepts reports. */
#define ZZB_ZCL_ILLUMINANCE_LEVEL_CLIENT()                                                         \
	{                                                                                          \
		.id = ZZB_ZCL_ILLUMINANCE_LEVEL,                                                   \
		.server = false,                                                                   \
		.def = &zzb_zcl_illuminance_level_client_def,                                      \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_ILLUMINANCE_H_ */
