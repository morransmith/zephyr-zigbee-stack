/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Metering cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_METERING_H_
#define ZZB_INCLUDE_ZZB_ZCL_METERING_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Metering cluster (ZCL 10.4). */
#define ZZB_ZCL_METERING 0x0702

/** Attributes (ZCL Tables 10-57, 10-61, 10-71 and 10-75). @{ */
#define ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED 0x0000
#define ZZB_ZCL_METERING_STATUS                      0x0200
#define ZZB_ZCL_METERING_UNIT_OF_MEASURE             0x0300
#define ZZB_ZCL_METERING_MULTIPLIER                  0x0301
#define ZZB_ZCL_METERING_DIVISOR                     0x0302
#define ZZB_ZCL_METERING_SUMMATION_FORMATTING        0x0303
#define ZZB_ZCL_METERING_DEMAND_FORMATTING           0x0304
#define ZZB_ZCL_METERING_DEVICE_TYPE                 0x0306
#define ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND        0x0400
/** @} */

/** Bits of Status of every commodity (ZCL Tables 10-62 to 10-65). @{ */
#define ZZB_ZCL_METERING_STATUS_CHECK_METER        BIT(0)
#define ZZB_ZCL_METERING_STATUS_LOW_BATTERY        BIT(1)
#define ZZB_ZCL_METERING_STATUS_TAMPER_DETECT      BIT(2)
#define ZZB_ZCL_METERING_STATUS_LEAK_DETECT        BIT(5)
#define ZZB_ZCL_METERING_STATUS_SERVICE_DISCONNECT BIT(6)
/** @} */

/** Bits of Status of electricity (ZCL Table 10-62). @{ */
#define ZZB_ZCL_METERING_STATUS_POWER_FAILURE BIT(3)
#define ZZB_ZCL_METERING_STATUS_POWER_QUALITY BIT(4)
/** @} */

/** Bits of Status of gas and water (ZCL Tables 10-63 and 10-64). @{ */
#define ZZB_ZCL_METERING_STATUS_LOW_PRESSURE BIT(4)
#define ZZB_ZCL_METERING_STATUS_REVERSE_FLOW BIT(7)
/** @} */

/** Bit of Status of water (ZCL Table 10-64). */
#define ZZB_ZCL_METERING_STATUS_PIPE_EMPTY BIT(3)

/** Bits of Status of heat and cooling (ZCL Table 10-65). @{ */
#define ZZB_ZCL_METERING_STATUS_TEMPERATURE_SENSOR BIT(3)
#define ZZB_ZCL_METERING_STATUS_BURST_DETECT       BIT(4)
#define ZZB_ZCL_METERING_STATUS_FLOW_SENSOR        BIT(7)
/** @} */

/**
 * UnitofMeasure (ZCL Table 10-72): the unit of a summation and of its rate, such as kWh and kW;
 * the values from 0x80 are in BCD.
 */
enum zzb_zcl_metering_unit_of_measure {
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KWH = 0x00,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_M3 = 0x01,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_FT3 = 0x02,
	/** 100 ft3. */
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_CCF = 0x03,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_US_GALLONS = 0x04,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_IMP_GALLONS = 0x05,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_BTU = 0x06,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_LITERS = 0x07,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KPA_GAUGE = 0x08,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KPA_ABSOLUTE = 0x09,
	/** 1000 ft3. */
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_MCF = 0x0a,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_UNITLESS = 0x0b,
	/** MJ and MJ/s. */
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_MJ = 0x0c,
	/** kVArh and kVAr. */
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KVAR = 0x0d,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KWH_BCD = 0x80,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_M3_BCD = 0x81,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_FT3_BCD = 0x82,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_CCF_BCD = 0x83,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_US_GALLONS_BCD = 0x84,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_IMP_GALLONS_BCD = 0x85,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_BTU_BCD = 0x86,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_LITERS_BCD = 0x87,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KPA_GAUGE_BCD = 0x88,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KPA_ABSOLUTE_BCD = 0x89,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_MCF_BCD = 0x8a,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_UNITLESS_BCD = 0x8b,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_MJ_BCD = 0x8c,
	ZZB_ZCL_METERING_UNIT_OF_MEASURE_KVAR_BCD = 0x8d,
};

/** MeteringDeviceType (ZCL Table 10-73); THERMAL and MIRRORED_THERMAL are deprecated. */
enum zzb_zcl_metering_device_type {
	ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC = 0x00,
	ZZB_ZCL_METERING_DEVICE_TYPE_GAS = 0x01,
	ZZB_ZCL_METERING_DEVICE_TYPE_WATER = 0x02,
	ZZB_ZCL_METERING_DEVICE_TYPE_THERMAL = 0x03,
	ZZB_ZCL_METERING_DEVICE_TYPE_PRESSURE = 0x04,
	ZZB_ZCL_METERING_DEVICE_TYPE_HEAT = 0x05,
	ZZB_ZCL_METERING_DEVICE_TYPE_COOLING = 0x06,
	/** End Use Measurement Device for the charging of electric vehicles. */
	ZZB_ZCL_METERING_DEVICE_TYPE_EUMD = 0x07,
	ZZB_ZCL_METERING_DEVICE_TYPE_PV_GENERATION = 0x08,
	ZZB_ZCL_METERING_DEVICE_TYPE_WIND_TURBINE_GENERATION = 0x09,
	ZZB_ZCL_METERING_DEVICE_TYPE_WATER_TURBINE_GENERATION = 0x0a,
	ZZB_ZCL_METERING_DEVICE_TYPE_MICRO_GENERATION = 0x0b,
	ZZB_ZCL_METERING_DEVICE_TYPE_SOLAR_HOT_WATER_GENERATION = 0x0c,
	ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC_PHASE_1 = 0x0d,
	ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC_PHASE_2 = 0x0e,
	ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC_PHASE_3 = 0x0f,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_ELECTRIC = 0x7f,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_GAS = 0x80,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_WATER = 0x81,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_THERMAL = 0x82,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_PRESSURE = 0x83,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_HEAT = 0x84,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_COOLING = 0x85,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_EUMD = 0x86,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_PV_GENERATION = 0x87,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_WIND_TURBINE_GENERATION = 0x88,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_WATER_TURBINE_GENERATION = 0x89,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_MICRO_GENERATION = 0x8a,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_SOLAR_HOT_WATER_GENERATION = 0x8b,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_ELECTRIC_PHASE_1 = 0x8c,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_ELECTRIC_PHASE_2 = 0x8d,
	ZZB_ZCL_METERING_DEVICE_TYPE_MIRRORED_ELECTRIC_PHASE_3 = 0x8e,
};

/** SummationFormatting or DemandFormatting with digits left and right of the decimal point. */
#define ZZB_ZCL_METERING_FORMATTING(left, right, suppress_zeros)                                   \
	(FIELD_PREP(GENMASK(6, 3), (left)) | FIELD_PREP(GENMASK(2, 0), (right)) |                  \
	 ((suppress_zeros) ? BIT(7) : 0))

/**
 * Values of a Metering server (ZCL 10.4.2.2). The application fills them before zzb_node_init()
 * and changes them with zzb_zcl_attr_set(); they are read-only. A summation or demand in the unit
 * of unit_of_measure is the value times multiplier divided by divisor.
 */
struct zzb_zcl_metering {
	/** 48 bits; the energy, gas or water delivered to the premises. */
	uint64_t current_summation_delivered;
	/** ZZB_ZCL_METERING_STATUS_* bits of the commodity. */
	uint8_t status;
	/** enum zzb_zcl_metering_unit_of_measure. */
	uint8_t unit_of_measure;
	/** 24 bits each. */
	uint32_t multiplier;
	uint32_t divisor;
	/** ZZB_ZCL_METERING_FORMATTING(). */
	uint8_t summation_formatting;
	uint8_t demand_formatting;
	/** enum zzb_zcl_metering_device_type. */
	uint8_t metering_device_type;
	/** 24 bits with sign; negative is demand received from the premises. */
	int32_t instantaneous_demand;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_metering_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_metering_client_def;

/** A Metering server with the values v, a pointer to struct zzb_zcl_metering. */
#define ZZB_ZCL_METERING_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_METERING,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_metering *)NULL),                             \
		.def = &zzb_zcl_metering_def,                                                      \
	}

/** A Metering client; it accepts reports and the commands of a Metering server. */
#define ZZB_ZCL_METERING_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_METERING,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_metering_client_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_METERING_H_ */
