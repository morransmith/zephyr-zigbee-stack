/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Window Covering cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_
#define ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Window Covering cluster (ZCL 7.4). */
#define ZZB_ZCL_WINDOW_COVERING 0x0102

/** Attributes (ZCL Tables 7-40 and 7-43). @{ */
#define ZZB_ZCL_WINDOW_COVERING_TYPE                        0x0000
#define ZZB_ZCL_WINDOW_COVERING_PHYSICAL_CLOSED_LIMIT_LIFT  0x0001
#define ZZB_ZCL_WINDOW_COVERING_PHYSICAL_CLOSED_LIMIT_TILT  0x0002
#define ZZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_LIFT       0x0003
#define ZZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_TILT       0x0004
#define ZZB_ZCL_WINDOW_COVERING_NUMBER_OF_ACTUATIONS_LIFT   0x0005
#define ZZB_ZCL_WINDOW_COVERING_NUMBER_OF_ACTUATIONS_TILT   0x0006
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_STATUS               0x0007
#define ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE             0x0008
#define ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE             0x0009
#define ZZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_LIFT   0x0010
#define ZZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_LIFT 0x0011
#define ZZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_TILT   0x0012
#define ZZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_TILT 0x0013
#define ZZB_ZCL_WINDOW_COVERING_MODE                        0x0017
/** @} */

/** Values of WindowCoveringType (ZCL Table 7-41). */
enum zzb_zcl_window_covering_type {
	ZZB_ZCL_WINDOW_COVERING_TYPE_ROLLERSHADE = 0x00,
	ZZB_ZCL_WINDOW_COVERING_TYPE_ROLLERSHADE_2_MOTOR = 0x01,
	ZZB_ZCL_WINDOW_COVERING_TYPE_ROLLERSHADE_EXTERIOR = 0x02,
	ZZB_ZCL_WINDOW_COVERING_TYPE_ROLLERSHADE_EXTERIOR_2_MOTOR = 0x03,
	ZZB_ZCL_WINDOW_COVERING_TYPE_DRAPERY = 0x04,
	ZZB_ZCL_WINDOW_COVERING_TYPE_AWNING = 0x05,
	ZZB_ZCL_WINDOW_COVERING_TYPE_SHUTTER = 0x06,
	ZZB_ZCL_WINDOW_COVERING_TYPE_TILT_BLIND_TILT_ONLY = 0x07,
	ZZB_ZCL_WINDOW_COVERING_TYPE_TILT_BLIND_LIFT_AND_TILT = 0x08,
	ZZB_ZCL_WINDOW_COVERING_TYPE_PROJECTOR_SCREEN = 0x09,
};

/** Bits of Config/Status (ZCL Table 7-42). @{ */
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_OPERATIONAL      BIT(0)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_ONLINE           BIT(1)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_LIFT_REVERSED    BIT(2)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_LIFT_CLOSED_LOOP BIT(3)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_TILT_CLOSED_LOOP BIT(4)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_LIFT_ENCODER     BIT(5)
#define ZZB_ZCL_WINDOW_COVERING_CONFIG_TILT_ENCODER     BIT(6)
/** @} */

/** CurrentPositionLiftPercentage and CurrentPositionTiltPercentage of an unknown position. @{ */
#define ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE_UNKNOWN 0xff
#define ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE_UNKNOWN 0xff
/** @} */

/** Bits of Mode (ZCL Table 7-44). @{ */
#define ZZB_ZCL_WINDOW_COVERING_MODE_REVERSED     BIT(0)
#define ZZB_ZCL_WINDOW_COVERING_MODE_CALIBRATION  BIT(1)
#define ZZB_ZCL_WINDOW_COVERING_MODE_MAINTENANCE  BIT(2)
#define ZZB_ZCL_WINDOW_COVERING_MODE_LED_FEEDBACK BIT(3)
/** @} */

/** Commands that the server receives and a client sends (ZCL Table 7-45). @{ */
#define ZZB_ZCL_WINDOW_COVERING_UP_OPEN               0x00
#define ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE            0x01
#define ZZB_ZCL_WINDOW_COVERING_STOP                  0x02
#define ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE      0x04
#define ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE 0x05
#define ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE      0x07
#define ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE 0x08
/** @} */

/**
 * Values of a Window Covering server (ZCL 7.4.2.1). The application fills the fields before mode
 * before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. Mode is
 * writable and saved. The commands and the positions of a recalled scene go to zcl_cmd_cb; the
 * application moves the covering and sets its position.
 */
struct zzb_zcl_window_covering {
	/**
	 * enum zzb_zcl_window_covering_type. It gives the axes of the Go To commands: tilt with
	 * Shutter and the Tilt Blind types, lift with every type except Shutter and Tilt Blind -
	 * Tilt Only.
	 */
	uint8_t window_covering_type;
	/** Centimeters. */
	uint16_t physical_closed_limit_lift;
	/** Tenths of a degree. */
	uint16_t physical_closed_limit_tilt;
	uint16_t current_position_lift;
	uint16_t current_position_tilt;
	uint16_t number_of_actuations_lift;
	uint16_t number_of_actuations_tilt;
	/** ZZB_ZCL_WINDOW_COVERING_CONFIG_* bits. */
	uint8_t config_status;
	/**
	 * CurrentPositionLiftPercentage and CurrentPositionTiltPercentage: 0 open to 100 closed, or
	 * ZZB_ZCL_WINDOW_COVERING_*_PERCENTAGE_UNKNOWN.
	 */
	uint8_t current_position_lift_percentage;
	uint8_t current_position_tilt_percentage;
	/** Centimeters; the ZCL defaults are 0x0000 and 0xffff. */
	uint16_t installed_open_limit_lift;
	uint16_t installed_closed_limit_lift;
	/** Tenths of a degree. */
	uint16_t installed_open_limit_tilt;
	uint16_t installed_closed_limit_tilt;
	/** ZZB_ZCL_WINDOW_COVERING_MODE_* bits; default 0. */
	uint8_t mode;
};

/** Internal: referenced by the instance macros of the clusters. @{ */
extern const struct zzb_zcl_cluster_def zzb_zcl_window_covering_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_window_covering_client_def;
/** @} */

/** A Window Covering server with the values v, a pointer to struct zzb_zcl_window_covering. */
#define ZZB_ZCL_WINDOW_COVERING_SERVER(v)                                                          \
	{                                                                                          \
		.id = ZZB_ZCL_WINDOW_COVERING,                                                     \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_window_covering *)NULL),                      \
		.def = &zzb_zcl_window_covering_def,                                               \
	}

/** A Window Covering client, which the Window Covering Controller has (DTL). */
#define ZZB_ZCL_WINDOW_COVERING_CLIENT()                                                           \
	{                                                                                          \
		.id = ZZB_ZCL_WINDOW_COVERING,                                                     \
		.server = false,                                                                   \
		.def = &zzb_zcl_window_covering_client_def,                                        \
	}

/**
 * Sends Go To Lift Percentage to the bound destinations: the covering moves to percentage, 0 open
 * to 100 closed (ZCL 7.4.2.2.5).
 *
 * @return the result of zzb_zcl_send_bound()
 */
static inline int zzb_zcl_window_covering_go_to_lift_percentage(struct zzb_node *node, uint8_t ep,
								uint8_t percentage)
{
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_WINDOW_COVERING,
				  ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, &percentage,
				  sizeof(percentage));
}

/**
 * Sends Go To Tilt Percentage to the bound destinations: the covering tilts to percentage, 0 open
 * to 100 closed (ZCL 7.4.2.2.7).
 *
 * @return the result of zzb_zcl_send_bound()
 */
static inline int zzb_zcl_window_covering_go_to_tilt_percentage(struct zzb_node *node, uint8_t ep,
								uint8_t percentage)
{
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_WINDOW_COVERING,
				  ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, &percentage,
				  sizeof(percentage));
}

/** Fields of Go To Lift Percentage (ZCL 7.4.2.2.5). */
struct zzb_zcl_window_covering_go_to_lift_percentage {
	/** 0 open to 100 closed. */
	uint8_t percentage;
};

/** Reads the fields of Go To Lift Percentage, which zcl_cmd_cb receives. */
static inline void zzb_zcl_window_covering_go_to_lift_percentage_parse(
	const struct zzb_zcl_cmd *cmd, struct zzb_zcl_window_covering_go_to_lift_percentage *fields)
{
	fields->percentage = cmd->payload[0];
}

/** Fields of Go To Tilt Percentage (ZCL 7.4.2.2.7). */
struct zzb_zcl_window_covering_go_to_tilt_percentage {
	/** 0 open to 100 closed. */
	uint8_t percentage;
};

/** Reads the fields of Go To Tilt Percentage, which zcl_cmd_cb receives. */
static inline void zzb_zcl_window_covering_go_to_tilt_percentage_parse(
	const struct zzb_zcl_cmd *cmd, struct zzb_zcl_window_covering_go_to_tilt_percentage *fields)
{
	fields->percentage = cmd->payload[0];
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_ */
