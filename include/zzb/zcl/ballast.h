/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_BALLAST_H_
#define ZZB_INCLUDE_ZZB_ZCL_BALLAST_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>
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

/** Bits of BallastStatus (ZCL Table 5.31). */
#define ZZB_ZCL_BALLAST_STATUS_NON_OPERATIONAL BIT(0)
#define ZZB_ZCL_BALLAST_STATUS_LAMP_FAILURE    BIT(1)

/** Bit of LampAlarmMode (ZCL Table 5.35). */
#define ZZB_ZCL_BALLAST_LAMP_ALARM_MODE_BURN_HOURS BIT(0)

/** The lowest and the highest level of the dimming light curve (ZCL 5.3.4). */
#define ZZB_ZCL_BALLAST_LEVEL_MIN 0x01
#define ZZB_ZCL_BALLAST_LEVEL_MAX 0xfe

/** IntrinsicBallastFactor that is not valid (ZCL 5.3.2.2.2.3). */
#define ZZB_ZCL_BALLAST_INTRINSIC_FACTOR_UNKNOWN 0xff

/** BallastFactorAdjustment that turns the scaling off (ZCL 5.3.2.2.2.4). */
#define ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT_OFF 0xff

/** LampRatedHours and LampBurnHours that are not valid or unknown (ZCL 5.3.2.2.4). */
#define ZZB_ZCL_BALLAST_LAMP_RATED_HOURS_UNKNOWN 0xffffff
#define ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_UNKNOWN  0xffffff

/** LampBurnHoursTripPoint that generates no alarm (ZCL 5.3.2.2.4.6). */
#define ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT_OFF 0xffffff

/**
 * Values of a Ballast Configuration server (ZCL 5.3.2.2). The application fills the first four
 * fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. The
 * other fields are writable and saved.
 */
struct zzb_zcl_ballast {
	/** ZZB_ZCL_BALLAST_LEVEL_MIN to ZZB_ZCL_BALLAST_LEVEL_MAX, the defaults of ZCL. */
	uint8_t physical_min_level;
	uint8_t physical_max_level;
	/** ZZB_ZCL_BALLAST_STATUS_* bits. */
	uint8_t ballast_status;
	uint8_t lamp_quantity;
	/** physical_min_level <= min_level <= max_level <= physical_max_level; default physical. */
	uint8_t min_level;
	uint8_t max_level;
	/** Percent, or ZZB_ZCL_BALLAST_INTRINSIC_FACTOR_UNKNOWN. */
	uint8_t intrinsic_ballast_factor;
	/** Percent, 0x64 and above, or ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT_OFF. */
	uint8_t ballast_factor_adjustment;
	/** Hours, or ZZB_ZCL_BALLAST_LAMP_RATED_HOURS_UNKNOWN. */
	uint32_t lamp_rated_hours;
	/**
	 * Hours, counted by the application with zzb_zcl_attr_set(), or
	 * ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_UNKNOWN.
	 */
	uint32_t lamp_burn_hours;
	/** ZZB_ZCL_BALLAST_LAMP_ALARM_MODE_* bits. */
	uint8_t lamp_alarm_mode;
	/** Hours, or ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT_OFF. */
	uint32_t lamp_burn_hours_trip_point;
	/** Internal: the condition of the lamp burn hours alarm lasts. */
	bool burn_hours_alarm;
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
