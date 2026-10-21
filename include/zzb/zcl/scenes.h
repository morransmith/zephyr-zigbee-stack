/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Scenes cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_SCENES_H_
#define ZZB_INCLUDE_ZZB_ZCL_SCENES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Scenes cluster (ZCL 3.7). */
#define ZZB_ZCL_SCENES 0x0005

/** Attributes (ZCL Table 3-40). @{ */
#define ZZB_ZCL_SCENES_SCENE_COUNT   0x0000
#define ZZB_ZCL_SCENES_CURRENT_SCENE 0x0001
#define ZZB_ZCL_SCENES_CURRENT_GROUP 0x0002
#define ZZB_ZCL_SCENES_SCENE_VALID   0x0003
/** @} */

/** The group ID of a scene without a group, in CurrentGroup and the commands (ZCL Table 3-41). */
#define ZZB_ZCL_SCENES_GROUP_NONE 0x0000

/** Bit of NameSupport (ZCL 3.7.2.2.1.5). */
#define ZZB_ZCL_SCENES_NAME_SUPPORT_NAMES BIT(7)

/** Commands received (ZCL Table 3-42); each response has the identifier of its command. @{ */
#define ZZB_ZCL_SCENES_ADD_SCENE            0x00
#define ZZB_ZCL_SCENES_VIEW_SCENE           0x01
#define ZZB_ZCL_SCENES_REMOVE_SCENE         0x02
#define ZZB_ZCL_SCENES_REMOVE_ALL_SCENES    0x03
#define ZZB_ZCL_SCENES_STORE_SCENE          0x04
#define ZZB_ZCL_SCENES_RECALL_SCENE         0x05
#define ZZB_ZCL_SCENES_GET_SCENE_MEMBERSHIP 0x06
#define ZZB_ZCL_SCENES_ENHANCED_ADD_SCENE   0x40
#define ZZB_ZCL_SCENES_ENHANCED_VIEW_SCENE  0x41
#define ZZB_ZCL_SCENES_COPY_SCENE           0x42
/** @} */

/**
 * Values of a Scenes server (ZCL 3.7.2.2), all read-only. The scene table of the node holds the
 * scenes; scene names are not stored.
 */
struct zzb_zcl_scenes {
	/** Scenes of the endpoint in the scene table. */
	uint8_t scene_count;
	uint8_t current_scene;
	/** ZZB_ZCL_SCENES_GROUP_NONE for a scene without a group. */
	uint16_t current_group;
	bool scene_valid;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_scenes_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_scenes_client_def;

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

/** A Scenes client; it accepts the responses to its commands. */
#define ZZB_ZCL_SCENES_CLIENT()                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_SCENES,                                                              \
		.server = false,                                                                   \
		.def = &zzb_zcl_scenes_client_def,                                                 \
	}

/**
 * @brief Send Store Scene to the bound destinations, which store their state as scene of group
 * (ZCL 3.7.2.4.6).
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_scenes_store_scene(struct zzb_node *node, uint8_t ep, uint16_t group,
					     uint8_t scene)
{
	uint8_t payload[3];

	sys_put_le16(group, payload);
	payload[2] = scene;
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_STORE_SCENE, payload,
				  sizeof(payload));
}

/**
 * @brief Send Recall Scene to the bound destinations, which recall scene of group
 * (ZCL 3.7.2.4.7).
 *
 * The optional transition time is left out, so the transition time of the scene applies.
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_scenes_recall_scene(struct zzb_node *node, uint8_t ep, uint16_t group,
					      uint8_t scene)
{
	uint8_t payload[3];

	sys_put_le16(group, payload);
	payload[2] = scene;
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_RECALL_SCENE, payload,
				  sizeof(payload));
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_SCENES_H_ */
