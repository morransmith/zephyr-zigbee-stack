/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_BALLAST_H_
#define ZZB_INCLUDE_ZZB_ZCL_BALLAST_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ballast Configuration cluster (ZCL 5.3). */
#define ZZB_ZCL_BALLAST 0x0301

/** Attributes (ZCL Tables 5.30, 5.32, 5.33 and 5.34). */
#define ZZB_ZCL_BALLAST_PHYSICAL_MIN_LEVEL         0x0000
#define ZZB_ZCL_BALLAST_PHYSICAL_MAX_LEVEL         0x0001
#define ZZB_ZCL_BALLAST_STATUS                     0x0002
#define ZZB_ZCL_BALLAST_MIN_LEVEL                  0x0010
#define ZZB_ZCL_BALLAST_MAX_LEVEL                  0x0011
#define ZZB_ZCL_BALLAST_INTRINSIC_FACTOR           0x0014
#define ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT          0x0015
#define ZZB_ZCL_BALLAST_LAMP_QUANTITY              0x0020
#define ZZB_ZCL_BALLAST_LAMP_RATED_HOURS           0x0032
#define ZZB_ZCL_BALLAST_LAMP_BURN_HOURS            0x0033
#define ZZB_ZCL_BALLAST_LAMP_ALARM_MODE            0x0034
#define ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT 0x0035

/**
 * Values of a Ballast Configuration server (ZCL 5.3.2.2). The application fills the first four
 * fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. The
 * other fields are writable and saved.
 */
struct zzb_zcl_ballast {
	/** 0x01 to 0xfe; the defaults of ZCL are 0x01 and 0xfe. */
	uint8_t physical_min_level;
	uint8_t physical_max_level;
	/** ZCL Table 5.31. */
	uint8_t ballast_status;
	uint8_t lamp_quantity;
	/** physical_min_level <= min_level <= max_level <= physical_max_level; default physical. */
	uint8_t min_level;
	uint8_t max_level;
	/** Percent, 0xff unknown. */
	uint8_t intrinsic_ballast_factor;
	/** Percent, 0x64 and above; 0xff turns the scaling off. */
	uint8_t ballast_factor_adjustment;
	/** Hours, 0xffffff unknown. */
	uint32_t lamp_rated_hours;
	/** Hours, counted by the application with zzb_zcl_attr_set(). */
	uint32_t lamp_burn_hours;
	uint8_t lamp_alarm_mode;
	/** Hours, 0xffffff disables the alarm. */
	uint32_t lamp_burn_hours_trip_point;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ballast_def;

/** A Ballast Configuration server with the values v, a pointer to struct zzb_zcl_ballast. */
#define ZZB_ZCL_BALLAST_SERVER(v)                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_BALLAST,                                                             \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_ballast *)NULL),                              \
		.def = &zzb_zcl_ballast_def,                                                       \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_BALLAST_H_ */
