/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_SCENES_H_
#define ZZB_INCLUDE_ZZB_ZCL_SCENES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Scenes cluster (ZCL 3.7). */
#define ZZB_ZCL_SCENES 0x0005

/** Attributes (ZCL Table 3-40). */
#define ZZB_ZCL_SCENES_SCENE_COUNT   0x0000
#define ZZB_ZCL_SCENES_CURRENT_SCENE 0x0001
#define ZZB_ZCL_SCENES_CURRENT_GROUP 0x0002
#define ZZB_ZCL_SCENES_SCENE_VALID   0x0003

/**
 * Values of a Scenes server (ZCL 3.7.2.2), all read-only. The scene table of the node holds the
 * scenes; scene names are not stored.
 */
struct zzb_zcl_scenes {
	/** Scenes of the endpoint in the scene table. */
	uint8_t scene_count;
	uint8_t current_scene;
	uint16_t current_group;
	bool scene_valid;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_scenes_def;

/**
 * A Scenes server with the values v, a pointer to struct zzb_zcl_scenes. ZCL 3.7.2.1 requires a
 * Groups server on the endpoint.
 */
#define ZZB_ZCL_SCENES_SERVER(v)                                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_SCENES,                                                              \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_scenes *)NULL),                               \
		.def = &zzb_zcl_scenes_def,                                                        \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_SCENES_H_ */
