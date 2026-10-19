/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_ON_OFF_H_
#define ZZB_INCLUDE_ZZB_ZCL_ON_OFF_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** On/Off cluster (ZCL 3.8). */
#define ZZB_ZCL_ON_OFF 0x0006

/** Attributes (ZCL Table 3-45). */
#define ZZB_ZCL_ON_OFF_ON_OFF               0x0000
#define ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL 0x4000
#define ZZB_ZCL_ON_OFF_ON_TIME              0x4001
#define ZZB_ZCL_ON_OFF_OFF_WAIT_TIME        0x4002

/** Values of an On/Off server (ZCL 3.8.2.2). OnOff is saved. */
struct zzb_zcl_on_off {
	bool on_off;
	bool global_scene_control;
	/** Tenths of a second. */
	uint16_t on_time;
	uint16_t off_wait_time;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_on_off_def;

/** An On/Off server with the values v, a pointer to struct zzb_zcl_on_off. */
#define ZZB_ZCL_ON_OFF_SERVER(v)                                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_ON_OFF,                                                              \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_on_off *)NULL),                               \
		.def = &zzb_zcl_on_off_def,                                                        \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_ON_OFF_H_ */
