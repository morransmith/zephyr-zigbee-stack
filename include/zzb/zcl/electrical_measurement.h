/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Electrical Measurement cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_ELECTRICAL_MEASUREMENT_H_
#define ZZB_INCLUDE_ZZB_ZCL_ELECTRICAL_MEASUREMENT_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Electrical Measurement cluster (ZCL 4.9). */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT 0x0b04

/** Attributes (ZCL Tables 4-29 and 4-33 to 4-36). @{ */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE        0x0000
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY            0x0300
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MIN        0x0301
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MAX        0x0302
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MULTIPLIER 0x0400
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_DIVISOR    0x0401
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE             0x0505
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_MIN         0x0506
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_MAX         0x0507
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT             0x0508
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_MIN         0x0509
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_MAX         0x050a
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER            0x050b
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_MIN        0x050c
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_MAX        0x050d
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER          0x050e
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER          0x050f
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_POWER_FACTOR            0x0510
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_MULTIPLIER   0x0600
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_DIVISOR      0x0601
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_CURRENT_MULTIPLIER   0x0602
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_CURRENT_DIVISOR      0x0603
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_POWER_MULTIPLIER     0x0604
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_POWER_DIVISOR        0x0605
/** @} */

/** Bits of MeasurementType (ZCL Table 4-30). @{ */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_ACTIVE        BIT(0)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_REACTIVE      BIT(1)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_APPARENT      BIT(2)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_PHASE_A       BIT(3)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_PHASE_B       BIT(4)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_PHASE_C       BIT(5)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_DC            BIT(6)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_HARMONICS     BIT(7)
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_POWER_QUALITY BIT(8)
/** @} */

/** A measurement that cannot be made, and its minimum and maximum before the first one. @{ */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN   0xffff
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN    0xffff
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN    0xffff
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN   INT16_MIN
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER_UNKNOWN INT16_MIN
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER_UNKNOWN 0xffff
/** @} */

/**
 * Values of an Electrical Measurement server (ZCL 4.9.2.2): the AC frequency and the AC
 * measurements of a single phase or phase A. The application fills them before zzb_node_init()
 * and changes them with zzb_zcl_attr_set(); they are read-only. A zeroed field is a valid
 * measurement, so the application sets a value it does not know to its _UNKNOWN value. A
 * measurement in its unit is the value times its multiplier divided by its divisor; a multiplier
 * or divisor of 0 is invalid, and ZCL gives them the default 1.
 */
struct zzb_zcl_electrical_measurement {
	/** ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_* bits. */
	uint32_t measurement_type;
	/** Hz or ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN. */
	uint16_t ac_frequency;
	uint16_t ac_frequency_min;
	uint16_t ac_frequency_max;
	uint16_t ac_frequency_multiplier;
	uint16_t ac_frequency_divisor;
	/** V or ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN. */
	uint16_t rms_voltage;
	uint16_t rms_voltage_min;
	uint16_t rms_voltage_max;
	/** A or ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN. */
	uint16_t rms_current;
	uint16_t rms_current_min;
	uint16_t rms_current_max;
	/**
	 * W, positive when delivered to the premises, or
	 * ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN.
	 */
	int16_t active_power;
	int16_t active_power_min;
	int16_t active_power_max;
	/**
	 * VAr, positive when delivered to the premises, or
	 * ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER_UNKNOWN.
	 */
	int16_t reactive_power;
	/** VA or ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER_UNKNOWN. */
	uint16_t apparent_power;
	/** -100 to 100 hundredths. */
	int8_t power_factor;
	uint16_t ac_voltage_multiplier;
	uint16_t ac_voltage_divisor;
	uint16_t ac_current_multiplier;
	uint16_t ac_current_divisor;
	/** Of the active, reactive and apparent power. */
	uint16_t ac_power_multiplier;
	uint16_t ac_power_divisor;
};

/** Internal: referenced by the instance macros of the clusters. @{ */
extern const struct zzb_zcl_cluster_def zzb_zcl_electrical_measurement_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_electrical_measurement_client_def;
/** @} */

/**
 * An Electrical Measurement server with the values v, a pointer to struct
 * zzb_zcl_electrical_measurement.
 */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(v)                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_ELECTRICAL_MEASUREMENT,                                              \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_electrical_measurement *)NULL),               \
		.def = &zzb_zcl_electrical_measurement_def,                                        \
	}

/** An Electrical Measurement client; it accepts reports and the commands of the server. */
#define ZZB_ZCL_ELECTRICAL_MEASUREMENT_CLIENT()                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_ELECTRICAL_MEASUREMENT,                                              \
		.server = false,                                                                   \
		.def = &zzb_zcl_electrical_measurement_client_def,                                 \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_ELECTRICAL_MEASUREMENT_H_ */
