/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_H_
#define ZZB_INCLUDE_ZZB_ZCL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zzb_node;

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

/** Command status values (ZCL Table 2-12). */
enum zzb_zcl_status {
	ZZB_ZCL_SUCCESS = 0x00,
	ZZB_ZCL_MALFORMED_COMMAND = 0x80,
	ZZB_ZCL_UNSUP_COMMAND = 0x81,
	ZZB_ZCL_INVALID_FIELD = 0x85,
	ZZB_ZCL_UNSUPPORTED_CLUSTER = 0xc3,
};

/** Returned by a cmd hook that has sent the response of the command itself. */
#define ZZB_ZCL_RESPONDED 0x100

/** A received cluster-specific command. */
struct zzb_zcl_cmd {
	/** The endpoint that processes the command. */
	uint8_t ep;
	uint16_t cluster;
	uint16_t src_addr;
	uint8_t src_ep;
	uint8_t tsn;
	/** Command identifier. */
	uint8_t id;
	const uint8_t *payload;
	size_t len;
};

/** The attributes and commands of a cluster. */
struct zzb_zcl_cluster_def {
	/**
	 * Processes a cluster-specific command on the stack thread, or NULL. Returns the status of
	 * the Default Response or ZZB_ZCL_RESPONDED.
	 */
	int (*cmd)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values);
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
