/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_
#define ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_

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

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_ */
