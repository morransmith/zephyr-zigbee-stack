/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_
#define ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
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

/** Bits of Options (ZCL Tables 3-57 and 3-144). */
#define ZZB_ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF    BIT(0)
#define ZZB_ZCL_LEVEL_OPTIONS_COUPLE_COLOR_TEMP BIT(1)

/** OnLevel without effect: On restores the level before Off (ZCL 3.10.2.2.10). */
#define ZZB_ZCL_LEVEL_ON_LEVEL_NONE 0xff

/** OnTransitionTime or OffTransitionTime that takes OnOffTransitionTime (ZCL 3.10.2.2.11). */
#define ZZB_ZCL_LEVEL_TRANSITION_TIME_NONE 0xffff

/** Values of StartUpCurrentLevel other than a level (ZCL Table 3-58). */
#define ZZB_ZCL_LEVEL_START_UP_MINIMUM  0x00
#define ZZB_ZCL_LEVEL_START_UP_PREVIOUS 0xff

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
	/** ZZB_ZCL_LEVEL_OPTIONS_* bits. */
	uint8_t options;
	uint16_t on_off_transition_time;
	/** A level or ZZB_ZCL_LEVEL_ON_LEVEL_NONE. */
	uint8_t on_level;
	/** Tenths of a second or ZZB_ZCL_LEVEL_TRANSITION_TIME_NONE. */
	uint16_t on_transition_time;
	uint16_t off_transition_time;
	/** A level, ZZB_ZCL_LEVEL_START_UP_MINIMUM or ZZB_ZCL_LEVEL_START_UP_PREVIOUS. */
	uint8_t start_up_current_level;
	/**
	 * Factory value of StartUpCurrentLevel, set by the application. A node without a record and
	 * the reset of the node take it.
	 */
	uint8_t start_up_default;
	/** Internal: the transition of CurrentLevel. */
	struct zzb_zcl_transition transition;
	/** Internal: the fade of On or Off or the change of OnOff at the end of the transition. */
	uint8_t end;
	/** Internal: the level that On and Off store (ZCL Table 3-55). */
	uint8_t stored_level;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_level_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_level_client_def;

/** A Level Control for Lighting server with the values v, a pointer to struct zzb_zcl_level. */
#define ZZB_ZCL_LEVEL_SERVER(v)                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_LEVEL,                                                               \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_level *)NULL),                                \
		.def = &zzb_zcl_level_def,                                                         \
	}

/** A Level Control client, which dimmer switches and controllers have (DTL). */
#define ZZB_ZCL_LEVEL_CLIENT()                                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_LEVEL,                                                               \
		.server = false,                                                                   \
		.def = &zzb_zcl_level_client_def,                                                  \
	}

/**
 * @brief Send Move to Level to the bound destinations (ZCL 3.10.2.3.1).
 *
 * CurrentLevel moves to level, 1 to 254 for lighting, over transition_time tenths of a second. The
 * optional OptionsMask and OptionsOverride are left out.
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_level_move_to_level(struct zzb_node *node, uint8_t ep, uint8_t level,
					      uint16_t transition_time)
{
	uint8_t payload[3] = {level};

	sys_put_le16(transition_time, payload + 1);
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL, payload,
				  sizeof(payload));
}

/**
 * @brief Send Move to Level (with On/Off) to the bound destinations (ZCL 3.10.2.3.6).
 *
 * As zzb_zcl_level_move_to_level(); the destinations also turn on for a level above the minimum
 * and off at the minimum.
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_level_move_to_level_on_off(struct zzb_node *node, uint8_t ep,
						     uint8_t level, uint16_t transition_time)
{
	uint8_t payload[3] = {level};

	sys_put_le16(transition_time, payload + 1);
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF,
				  payload, sizeof(payload));
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_LEVEL_H_ */
