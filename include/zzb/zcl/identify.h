/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IDENTIFY_H_
#define ZZB_INCLUDE_ZZB_ZCL_IDENTIFY_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identify cluster (ZCL 3.5). */
#define ZZB_ZCL_IDENTIFY 0x0003

/** IdentifyTime attribute. */
#define ZZB_ZCL_IDENTIFY_TIME 0x0000

/** Values of an Identify server (ZCL 3.5.2.2). */
struct zzb_zcl_identify {
	/** Seconds of identification left; the node identifies itself while it is not 0. */
	uint16_t identify_time;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_identify_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_identify_client_def;

/** An Identify server with the values v, a pointer to struct zzb_zcl_identify. */
#define ZZB_ZCL_IDENTIFY_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_IDENTIFY,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_identify *)NULL),                             \
		.def = &zzb_zcl_identify_def,                                                      \
	}

/**
 * An Identify client. Finding & binding for an initiator needs it on its endpoint: it passes
 * Identify Query Response to BDB.
 */
#define ZZB_ZCL_IDENTIFY_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_IDENTIFY,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_identify_client_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IDENTIFY_H_ */
