/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_
#define ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Groups cluster (ZCL 3.6). */
#define ZZB_ZCL_GROUPS 0x0004

/** Commands received (ZCL Table 3-37); each response has the identifier of its command. */
#define ZZB_ZCL_GROUPS_ADD_GROUP                0x00
#define ZZB_ZCL_GROUPS_VIEW_GROUP               0x01
#define ZZB_ZCL_GROUPS_GET_GROUP_MEMBERSHIP     0x02
#define ZZB_ZCL_GROUPS_REMOVE_GROUP             0x03
#define ZZB_ZCL_GROUPS_REMOVE_ALL_GROUPS        0x04
#define ZZB_ZCL_GROUPS_ADD_GROUP_IF_IDENTIFYING 0x05

/** Bit of NameSupport (ZCL 3.6.2.2.1). */
#define ZZB_ZCL_GROUPS_NAME_SUPPORT_NAMES BIT(7)

/** The longest group name, in characters (ZCL 3.6.2.2.2). */
#define ZZB_ZCL_GROUPS_NAME_MAX 16

extern const struct zzb_zcl_cluster_def zzb_zcl_groups_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_groups_client_def;

/**
 * A Groups server. It keeps the memberships in the APS group table and no group names. ZCL 3.6.2.1
 * requires an Identify server on the endpoint.
 */
#define ZZB_ZCL_GROUPS_SERVER()                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_GROUPS,                                                              \
		.server = true,                                                                    \
		.def = &zzb_zcl_groups_def,                                                        \
	}

/** A Groups client, which controllers have (DTL). */
#define ZZB_ZCL_GROUPS_CLIENT()                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_GROUPS,                                                              \
		.server = false,                                                                   \
		.def = &zzb_zcl_groups_client_def,                                                 \
	}

/**
 * @brief Send Add Group to the bound destinations, which join group (ZCL 3.6.2.3.2).
 *
 * The name has len characters.
 *
 * @retval -EINVAL the name is longer than ZZB_ZCL_GROUPS_NAME_MAX.
 * @return otherwise the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_groups_add_group(struct zzb_node *node, uint8_t ep, uint16_t group,
					   const char *name, size_t len)
{
	/* Group ID, the length octet and the name */
	uint8_t payload[3 + ZZB_ZCL_GROUPS_NAME_MAX];

	if (len > ZZB_ZCL_GROUPS_NAME_MAX) {
		return -EINVAL;
	}

	sys_put_le16(group, payload);
	payload[2] = len;
	memcpy(payload + 3, name, len);
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_GROUPS, ZZB_ZCL_GROUPS_ADD_GROUP, payload,
				  3 + len);
}

/**
 * @brief Send Remove Group to the bound destinations, which leave group (ZCL 3.6.2.3.5).
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_groups_remove_group(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	uint8_t payload[2];

	sys_put_le16(group, payload);
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_GROUPS, ZZB_ZCL_GROUPS_REMOVE_GROUP, payload,
				  sizeof(payload));
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_ */
