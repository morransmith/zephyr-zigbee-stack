/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Temperature, Pressure and Flow Measurement clusters.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_MEASUREMENT_H_
#define ZZB_INCLUDE_ZZB_ZCL_MEASUREMENT_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Temperature Measurement cluster (ZCL 4.4). */
#define ZZB_ZCL_TEMPERATURE 0x0402
/** Pressure Measurement cluster (ZCL 4.5). */
#define ZZB_ZCL_PRESSURE    0x0403
/** Flow Measurement cluster (ZCL 4.6). */
#define ZZB_ZCL_FLOW        0x0404
/** Relative Humidity Measurement cluster (ZCL 4.7). */
#define ZZB_ZCL_HUMIDITY    0x0405

/** Attributes of the four clusters (ZCL Tables 4-13, 4-15, 4-18 and 4-19). */
#define ZZB_ZCL_MEASUREMENT_MEASURED_VALUE     0x0000
#define ZZB_ZCL_MEASUREMENT_MIN_MEASURED_VALUE 0x0001
#define ZZB_ZCL_MEASUREMENT_MAX_MEASURED_VALUE 0x0002
#define ZZB_ZCL_MEASUREMENT_TOLERANCE          0x0003

/** Attributes of the extended set of Pressure Measurement (ZCL Table 4-16). */
#define ZZB_ZCL_PRESSURE_SCALED_VALUE     0x0010
#define ZZB_ZCL_PRESSURE_MIN_SCALED_VALUE 0x0011
#define ZZB_ZCL_PRESSURE_MAX_SCALED_VALUE 0x0012
#define ZZB_ZCL_PRESSURE_SCALED_TOLERANCE 0x0013
#define ZZB_ZCL_PRESSURE_SCALE            0x0014

/*
 * The values of the measurement servers are read-only. The application fills them before
 * zzb_node_init() and changes them with zzb_zcl_attr_set(). A zeroed field is a valid measurement,
 * so the application sets a value it does not know to the _UNKNOWN or _NOT_DEFINED name of its
 * attribute.
 */

/** MeasuredValue of an unknown temperature and the limits of an unknown range (ZCL 4.4.2.2.1). */
#define ZZB_ZCL_TEMPERATURE_MEASURED_VALUE_UNKNOWN     INT16_MIN
#define ZZB_ZCL_TEMPERATURE_MIN_MEASURED_VALUE_UNKNOWN INT16_MIN
#define ZZB_ZCL_TEMPERATURE_MAX_MEASURED_VALUE_UNKNOWN INT16_MIN

/** Values of a Temperature Measurement server (ZCL 4.4.2.2). */
struct zzb_zcl_temperature {
	/** 100 x degrees Celsius. */
	int16_t measured_value;
	int16_t min_measured_value;
	int16_t max_measured_value;
	uint16_t tolerance;
};

/** MeasuredValue of an unknown pressure and the limits that are not defined (ZCL 4.5.2.2.1). */
#define ZZB_ZCL_PRESSURE_MEASURED_VALUE_UNKNOWN         INT16_MIN
#define ZZB_ZCL_PRESSURE_MIN_MEASURED_VALUE_NOT_DEFINED INT16_MIN
#define ZZB_ZCL_PRESSURE_MAX_MEASURED_VALUE_NOT_DEFINED INT16_MIN

/** ScaledValue of an invalid measurement and the limits that are not defined (ZCL 4.5.2.2.2). */
#define ZZB_ZCL_PRESSURE_SCALED_VALUE_UNKNOWN         INT16_MIN
#define ZZB_ZCL_PRESSURE_MIN_SCALED_VALUE_NOT_DEFINED INT16_MIN
#define ZZB_ZCL_PRESSURE_MAX_SCALED_VALUE_NOT_DEFINED INT16_MIN

/** Values of a Pressure Measurement server (ZCL 4.5.2.2). */
struct zzb_zcl_pressure {
	/** 10 x kPa. */
	int16_t measured_value;
	int16_t min_measured_value;
	int16_t max_measured_value;
	uint16_t tolerance;
	/** 10^scale x Pa. */
	int16_t scaled_value;
	int16_t min_scaled_value;
	int16_t max_scaled_value;
	uint16_t scaled_tolerance;
	int8_t scale;
};

/** MeasuredValue of an unknown flow and the limits that are not defined (ZCL 4.6.2.2.1). */
#define ZZB_ZCL_FLOW_MEASURED_VALUE_UNKNOWN         0xffff
#define ZZB_ZCL_FLOW_MIN_MEASURED_VALUE_NOT_DEFINED 0xffff
#define ZZB_ZCL_FLOW_MAX_MEASURED_VALUE_NOT_DEFINED 0xffff

/** Values of a Flow Measurement server (ZCL 4.6.2.2). */
struct zzb_zcl_flow {
	/** 10 x m3/h. */
	uint16_t measured_value;
	uint16_t min_measured_value;
	uint16_t max_measured_value;
	uint16_t tolerance;
};

/** MeasuredValue of an unknown humidity and the limits that are not defined (ZCL 4.7.2.1). */
#define ZZB_ZCL_HUMIDITY_MEASURED_VALUE_UNKNOWN         0xffff
#define ZZB_ZCL_HUMIDITY_MIN_MEASURED_VALUE_NOT_DEFINED 0xffff
#define ZZB_ZCL_HUMIDITY_MAX_MEASURED_VALUE_NOT_DEFINED 0xffff

/** Values of a Relative Humidity Measurement server (ZCL 4.7.2.1). */
struct zzb_zcl_humidity {
	/** 100 x percent. */
	uint16_t measured_value;
	uint16_t min_measured_value;
	uint16_t max_measured_value;
	uint16_t tolerance;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_temperature_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_pressure_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_flow_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_humidity_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_temperature_client_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_pressure_client_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_flow_client_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_humidity_client_def;

/** A Temperature Measurement server with the values v, a pointer to struct zzb_zcl_temperature. */
#define ZZB_ZCL_TEMPERATURE_SERVER(v)                                                              \
	{                                                                                          \
		.id = ZZB_ZCL_TEMPERATURE,                                                         \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_temperature *)NULL),                          \
		.def = &zzb_zcl_temperature_def,                                                   \
	}

/** A Pressure Measurement server with the values v, a pointer to struct zzb_zcl_pressure. */
#define ZZB_ZCL_PRESSURE_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_PRESSURE,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_pressure *)NULL),                             \
		.def = &zzb_zcl_pressure_def,                                                      \
	}

/** A Flow Measurement server with the values v, a pointer to struct zzb_zcl_flow. */
#define ZZB_ZCL_FLOW_SERVER(v)                                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_FLOW,                                                                \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_flow *)NULL),                                 \
		.def = &zzb_zcl_flow_def,                                                          \
	}

/**
 * A Relative Humidity Measurement server with the values v, a pointer to struct zzb_zcl_humidity.
 */
#define ZZB_ZCL_HUMIDITY_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_HUMIDITY,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_humidity *)NULL),                             \
		.def = &zzb_zcl_humidity_def,                                                      \
	}

/** A Temperature Measurement client; it accepts reports. */
#define ZZB_ZCL_TEMPERATURE_CLIENT()                                                               \
	{                                                                                          \
		.id = ZZB_ZCL_TEMPERATURE,                                                         \
		.server = false,                                                                   \
		.def = &zzb_zcl_temperature_client_def,                                            \
	}

/** A Pressure Measurement client; it accepts reports. */
#define ZZB_ZCL_PRESSURE_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_PRESSURE,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_pressure_client_def,                                               \
	}

/** A Flow Measurement client; it accepts reports. */
#define ZZB_ZCL_FLOW_CLIENT()                                                                      \
	{                                                                                          \
		.id = ZZB_ZCL_FLOW,                                                                \
		.server = false,                                                                   \
		.def = &zzb_zcl_flow_client_def,                                                   \
	}

/** A Relative Humidity Measurement client; it accepts reports. */
#define ZZB_ZCL_HUMIDITY_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_HUMIDITY,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_humidity_client_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_MEASUREMENT_H_ */
