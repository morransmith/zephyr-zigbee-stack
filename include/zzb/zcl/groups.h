/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_
#define ZZB_INCLUDE_ZZB_ZCL_GROUPS_H_

#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Groups cluster (ZCL 3.6). */
#define ZZB_ZCL_GROUPS 0x0004

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
