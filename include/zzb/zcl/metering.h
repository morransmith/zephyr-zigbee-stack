/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_METERING_H_
#define ZZB_INCLUDE_ZZB_ZCL_METERING_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Metering cluster (ZCL 10.4). */
#define ZZB_ZCL_METERING 0x0702

/** Attributes (ZCL Tables 10-57, 10-61, 10-71 and 10-75). */
#define ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED 0x0000
#define ZZB_ZCL_METERING_STATUS                      0x0200
#define ZZB_ZCL_METERING_UNIT_OF_MEASURE             0x0300
#define ZZB_ZCL_METERING_MULTIPLIER                  0x0301
#define ZZB_ZCL_METERING_DIVISOR                     0x0302
#define ZZB_ZCL_METERING_SUMMATION_FORMATTING        0x0303
#define ZZB_ZCL_METERING_DEMAND_FORMATTING           0x0304
#define ZZB_ZCL_METERING_DEVICE_TYPE                 0x0306
#define ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND        0x0400

/**
 * Values of a Metering server (ZCL 10.4.2.2). The application fills them before zzb_node_init()
 * and changes them with zzb_zcl_attr_set(); they are read-only. A summation or demand in the unit
 * of unit_of_measure is the value times multiplier divided by divisor.
 */
struct zzb_zcl_metering {
	/** 48 bits; the energy, gas or water delivered to the premises. */
	uint64_t current_summation_delivered;
	/** Error and warning bits of the commodity (ZCL Tables 10-62 to 10-65). */
	uint8_t status;
	/** ZCL Table 10-72, such as 0x00 kWh and kW or 0x01 m3 and m3/h; 0x80 and above are BCD. */
	uint8_t unit_of_measure;
	/** 24 bits each. */
	uint32_t multiplier;
	uint32_t divisor;
	/**
	 * Bits 0 to 2: digits right of the decimal point, bits 3 to 6: digits left of it, bit 7:
	 * suppress leading zeros (ZCL 10.4.2.2.4.4).
	 */
	uint8_t summation_formatting;
	uint8_t demand_formatting;
	/** ZCL Table 10-73, such as 0 electric, 1 gas or 2 water metering. */
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
