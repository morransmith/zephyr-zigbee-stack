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
#define ZZB_ZCL_ON_OFF_START_UP             0x4003

/** Values of StartUpOnOff (ZCL Table 3-46). */
enum zzb_zcl_start_up_on_off {
	ZZB_ZCL_START_UP_OFF = 0x00,
	ZZB_ZCL_START_UP_ON = 0x01,
	ZZB_ZCL_START_UP_TOGGLE = 0x02,
	ZZB_ZCL_START_UP_PREVIOUS = 0xff,
};

/** Values of an On/Off server (ZCL 3.8.2.2). OnOff and StartUpOnOff are saved. */
struct zzb_zcl_on_off {
	bool on_off;
	bool global_scene_control;
	/** Tenths of a second. */
	uint16_t on_time;
	uint16_t off_wait_time;
	uint8_t start_up_on_off;
	/**
	 * Factory value of StartUpOnOff, set by the application. A node without a record and the
	 * reset of the node take it.
	 */
	uint8_t start_up_default;
	/** Internal: OnOff of the global scene on an endpoint without a Scenes server. */
	bool global_scene;
	/** Internal: the update of On With Timed Off runs. */
	bool timed;
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
