/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_H_
#define ZZB_INCLUDE_ZZB_ZCL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zzb_zcl_cluster_def;

/** Data types of the attributes of the stack (ZCL Table 2-11). */
enum zzb_zcl_type {
	ZZB_ZCL_BOOL = 0x10,
	ZZB_ZCL_MAP8 = 0x18,
	ZZB_ZCL_UINT8 = 0x20,
	ZZB_ZCL_UINT16 = 0x21,
	ZZB_ZCL_ENUM8 = 0x30,
	ZZB_ZCL_OCTSTR = 0x41,
	ZZB_ZCL_STRING = 0x42,
};

/** A cluster on an endpoint. */
struct zzb_zcl_cluster {
	uint16_t id;
	/** A server is an input cluster of the simple descriptor, a client an output cluster. */
	bool server;
	/** Attribute values, of the struct type that the definition expects. */
	void *values;
	/** Attributes and commands, or NULL for a cluster without them. */
	const struct zzb_zcl_cluster_def *def;
};

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_H_ */
