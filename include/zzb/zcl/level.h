/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_
#define ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Level Control for Lighting cluster (ZCL 3.10, 3.19). */
#define ZZB_ZCL_LEVEL 0x0008

/** Attributes (ZCL Table 3-56). */
#define ZZB_ZCL_LEVEL_CURRENT_LEVEL          0x0000
#define ZZB_ZCL_LEVEL_REMAINING_TIME         0x0001
#define ZZB_ZCL_LEVEL_OPTIONS                0x000f
#define ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME 0x0010
#define ZZB_ZCL_LEVEL_ON_LEVEL               0x0011
#define ZZB_ZCL_LEVEL_ON_TRANSITION_TIME     0x0012
#define ZZB_ZCL_LEVEL_OFF_TRANSITION_TIME    0x0013
#define ZZB_ZCL_LEVEL_START_UP               0x4000

/** Commands that the server receives and a client sends (ZCL Table 3-59). */
#define ZZB_ZCL_LEVEL_MOVE_TO_LEVEL        0x00
#define ZZB_ZCL_LEVEL_MOVE                 0x01
#define ZZB_ZCL_LEVEL_STEP                 0x02
#define ZZB_ZCL_LEVEL_STOP                 0x03
#define ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF 0x04
#define ZZB_ZCL_LEVEL_MOVE_ON_OFF          0x05
#define ZZB_ZCL_LEVEL_STEP_ON_OFF          0x06
#define ZZB_ZCL_LEVEL_STOP_ON_OFF          0x07

/**
 * Values of a Level Control for Lighting server (ZCL 3.10.2.2, 3.19.2.2). CurrentLevel and the
 * writable attributes are saved.
 */
struct zzb_zcl_level {
	/** 1 to 254. */
	uint8_t current_level;
	/** Tenths of a second, as the transition times. */
	uint16_t remaining_time;
	uint8_t options;
	uint16_t on_off_transition_time;
	uint8_t on_level;
	uint16_t on_transition_time;
	uint16_t off_transition_time;
	uint8_t start_up_current_level;
	/**
	 * Factory value of StartUpCurrentLevel, set by the application. A node without a record and
	 * the reset of the node take it.
	 */
	uint8_t start_up_default;
	/** Internal: the transition of CurrentLevel. */
	struct zzb_zcl_transition transition;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_level_def;

/** A Level Control for Lighting server with the values v, a pointer to struct zzb_zcl_level. */
#define ZZB_ZCL_LEVEL_SERVER(v)                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_LEVEL,                                                               \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_level *)NULL),                                \
		.def = &zzb_zcl_level_def,                                                         \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_ */
