/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Fan Control cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_FAN_H_
#define ZZB_INCLUDE_ZZB_ZCL_FAN_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fan Control cluster (ZCL 6.4). */
#define ZZB_ZCL_FAN 0x0202

/** Attributes (ZCL Table 6-40). @{ */
#define ZZB_ZCL_FAN_MODE          0x0000
#define ZZB_ZCL_FAN_MODE_SEQUENCE 0x0001
/** @} */

/** Values of FanMode (ZCL Table 6-41). */
enum zzb_zcl_fan_mode {
	ZZB_ZCL_FAN_MODE_OFF = 0x00,
	ZZB_ZCL_FAN_MODE_LOW = 0x01,
	ZZB_ZCL_FAN_MODE_MEDIUM = 0x02,
	ZZB_ZCL_FAN_MODE_HIGH = 0x03,
	ZZB_ZCL_FAN_MODE_ON = 0x04,
	ZZB_ZCL_FAN_MODE_AUTO = 0x05,
	ZZB_ZCL_FAN_MODE_SMART = 0x06,
};

/** Values of FanModeSequence (ZCL Table 6-42). */
enum zzb_zcl_fan_mode_sequence {
	ZZB_ZCL_FAN_MODE_SEQUENCE_LOW_MED_HIGH = 0x00,
	ZZB_ZCL_FAN_MODE_SEQUENCE_LOW_HIGH = 0x01,
	ZZB_ZCL_FAN_MODE_SEQUENCE_LOW_MED_HIGH_AUTO = 0x02,
	ZZB_ZCL_FAN_MODE_SEQUENCE_LOW_HIGH_AUTO = 0x03,
	ZZB_ZCL_FAN_MODE_SEQUENCE_ON_AUTO = 0x04,
};

/**
 * Values of a Fan Control server (ZCL 6.4.2.1). FanMode is writable and saved. The application
 * sets fan_mode_sequence before zzb_node_init() and changes it with zzb_zcl_attr_set(); it is
 * read-only.
 */
struct zzb_zcl_fan {
	/** enum zzb_zcl_fan_mode; the application resolves Smart by occupancy. */
	uint8_t fan_mode;
	/** enum zzb_zcl_fan_mode_sequence; Low/Med/High/Auto is the default of ZCL. */
	uint8_t fan_mode_sequence;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_fan_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_fan_client_def;

/** A Fan Control server with the values v, a pointer to struct zzb_zcl_fan. */
#define ZZB_ZCL_FAN_SERVER(v)                                                                      \
	{                                                                                          \
		.id = ZZB_ZCL_FAN,                                                                 \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_fan *)NULL),                                  \
		.def = &zzb_zcl_fan_def,                                                           \
	}

/** A Fan Control client; it accepts reports. */
#define ZZB_ZCL_FAN_CLIENT()                                                                       \
	{                                                                                          \
		.id = ZZB_ZCL_FAN,                                                                 \
		.server = false,                                                                   \
		.def = &zzb_zcl_fan_client_def,                                                    \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_FAN_H_ */
