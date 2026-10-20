/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_
#define ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Window Covering cluster (ZCL 7.4). */
#define ZZB_ZCL_WINDOW_COVERING 0x0102

/** Attributes (ZCL Tables 7-40 and 7-43). */
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

/** Commands that the server receives and a client sends (ZCL Table 7-45). */
#define ZZB_ZCL_WINDOW_COVERING_UP_OPEN    0x00
#define ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE 0x01
#define ZZB_ZCL_WINDOW_COVERING_STOP       0x02

/**
 * Values of a Window Covering server (ZCL 7.4.2.1). The application fills the fields before mode
 * before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. Mode is
 * writable and saved. The commands go to zcl_cmd_cb; the application moves the covering and sets
 * its position.
 */
struct zzb_zcl_window_covering {
	/** ZCL Table 7-41. */
	uint8_t window_covering_type;
	/** Centimeters. */
	uint16_t physical_closed_limit_lift;
	/** Tenths of a degree. */
	uint16_t physical_closed_limit_tilt;
	uint16_t current_position_lift;
	uint16_t current_position_tilt;
	uint16_t number_of_actuations_lift;
	uint16_t number_of_actuations_tilt;
	/** ZCL Table 7-42. */
	uint8_t config_status;
	/** CurrentPositionLiftPercentage: 0 open to 100 closed, 0xff unknown. */
	uint8_t current_position_lift_percentage;
	uint8_t current_position_tilt_percentage;
	/** Centimeters; the ZCL defaults are 0x0000 and 0xffff. */
	uint16_t installed_open_limit_lift;
	uint16_t installed_closed_limit_lift;
	/** Tenths of a degree. */
	uint16_t installed_open_limit_tilt;
	uint16_t installed_closed_limit_tilt;
	/** ZCL Table 7-44, bits 0 to 3; default 0. */
	uint8_t mode;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_window_covering_def;

/** A Window Covering server with the values v, a pointer to struct zzb_zcl_window_covering. */
#define ZZB_ZCL_WINDOW_COVERING_SERVER(v)                                                          \
	{                                                                                          \
		.id = ZZB_ZCL_WINDOW_COVERING,                                                     \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_window_covering *)NULL),                      \
		.def = &zzb_zcl_window_covering_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_WINDOW_COVERING_H_ */
