/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_ON_OFF_SWITCH_CONFIG_H_
#define ZZB_INCLUDE_ZZB_ZCL_ON_OFF_SWITCH_CONFIG_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** On/Off Switch Configuration cluster (ZCL 3.9). */
#define ZZB_ZCL_ON_OFF_SWITCH_CONFIG 0x0007

/** Attributes (ZCL Tables 3-51 and 3-53). */
#define ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_TYPE    0x0000
#define ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS 0x0010

/**
 * Values of an On/Off Switch Configuration server (ZCL 3.9.2.2). The application sets switch_type
 * before zzb_node_init() and changes it with zzb_zcl_attr_set(); it is read-only. SwitchActions is
 * writable and saved. The application sends the On/Off commands of ZCL Table 3-54 with
 * zzb_zcl_send_bound() from the On/Off client of the endpoint, which ZCL 3.9.2.1 requires.
 */
struct zzb_zcl_on_off_switch_config {
	/** ZCL Table 3-52: 0x00 toggle, 0x01 momentary, 0x02 multifunction. */
	uint8_t switch_type;
	/** ZCL Table 3-54: 0x00 On and Off, 0x01 Off and On, 0x02 Toggle and Toggle. */
	uint8_t switch_actions;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_on_off_switch_config_def;

/**
 * An On/Off Switch Configuration server with the values v, a pointer to struct
 * zzb_zcl_on_off_switch_config.
 */
#define ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SERVER(v)                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_ON_OFF_SWITCH_CONFIG,                                                \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_on_off_switch_config *)NULL),                 \
		.def = &zzb_zcl_on_off_switch_config_def,                                          \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_ON_OFF_SWITCH_CONFIG_H_ */
