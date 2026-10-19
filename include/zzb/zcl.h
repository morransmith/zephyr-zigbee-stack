/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_H_
#define ZZB_INCLUDE_ZZB_ZCL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

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

#define ZZB_ZCL_IS_STRING(type) ((type) == ZZB_ZCL_OCTSTR || (type) == ZZB_ZCL_STRING)
#define ZZB_ZCL_IS_BYTE(type)                                                                      \
	((type) == ZZB_ZCL_BOOL || (type) == ZZB_ZCL_MAP8 || (type) == ZZB_ZCL_UINT8 ||            \
	 (type) == ZZB_ZCL_ENUM8)

/** Size of the value struct field of a type; 0 for a type that the stack does not store. */
#define ZZB_ZCL_STORAGE_SIZE(type)                                                                 \
	((type) == ZZB_ZCL_UINT16  ? 2                                                             \
	 : ZZB_ZCL_IS_STRING(type) ? sizeof(void *)                                                \
				   : ZZB_ZCL_IS_BYTE(type))

/** zzb_zcl_attr.flags; every attribute is readable. */
#define ZZB_ZCL_WRITE  BIT(0)
#define ZZB_ZCL_REPORT BIT(1)
/** The descriptor holds the value of the attribute. */
#define ZZB_ZCL_CONST  BIT(2)
/** The attribute is saved in the settings at every change. */
#define ZZB_ZCL_NV     BIT(3)

/**
 * An attribute. A character string is stored as a const char pointer, an octet string as a const
 * uint8_t pointer to its length octet; both are read-only, and NULL reads as an empty string.
 */
struct zzb_zcl_attr {
	uint16_t id;
	uint8_t type;
	uint8_t flags;
	union {
		/** Offset of the field in the value struct. */
		uint16_t offset;
		/** Value of a constant attribute. */
		uint16_t value;
	};
};

/**
 * An attribute stored in the field _field of _struct. BUILD_ASSERT cannot stand in an initializer,
 * so the size of the field is checked through the offset.
 */
#define ZZB_ZCL_ATTR(_id, _type, _flags, _struct, _field)                                          \
	{                                                                                          \
		.id = (_id),                                                                       \
		.type = (_type),                                                                   \
		.flags = (_flags),                                                                 \
		.offset = offsetof(_struct, _field) +                                              \
			  ZERO_OR_COMPILE_ERROR(SIZEOF_FIELD(_struct, _field) ==                   \
						ZZB_ZCL_STORAGE_SIZE(_type)),                      \
	}

/** A constant attribute of an integer type of at most 16 bits. */
#define ZZB_ZCL_ATTR_CONST(_id, _type, _value)                                                     \
	{                                                                                          \
		.id = (_id),                                                                       \
		.type = (_type),                                                                   \
		.flags = ZZB_ZCL_CONST,                                                            \
		.value = (_value),                                                                 \
	}

/** Command status values (ZCL Table 2-12). */
enum zzb_zcl_status {
	ZZB_ZCL_SUCCESS = 0x00,
	ZZB_ZCL_MALFORMED_COMMAND = 0x80,
	ZZB_ZCL_UNSUP_COMMAND = 0x81,
	ZZB_ZCL_INVALID_FIELD = 0x85,
	ZZB_ZCL_UNSUPPORTED_ATTRIBUTE = 0x86,
	ZZB_ZCL_INVALID_VALUE = 0x87,
	ZZB_ZCL_READ_ONLY = 0x88,
	ZZB_ZCL_INSUFFICIENT_SPACE = 0x89,
	ZZB_ZCL_INVALID_DATA_TYPE = 0x8d,
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
	/** Attributes in ascending order of ID. */
	const struct zzb_zcl_attr *attrs;
	uint8_t attr_count;
	/** Restores the defaults at the reset of the node, on the stack thread, or NULL. */
	void (*reset)(struct zzb_node *node, uint8_t ep, void *values);
	/**
	 * Processes a cluster-specific command on the stack thread, or NULL. Returns the status of
	 * the Default Response or ZZB_ZCL_RESPONDED.
	 */
	int (*cmd)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values);
	/**
	 * Checks a remote write of value to attr, or NULL. Returns ZZB_ZCL_SUCCESS or
	 * ZZB_ZCL_INVALID_VALUE.
	 */
	uint8_t (*check)(const struct zzb_zcl_attr *attr, uint16_t value);
	/** Called on the stack thread after every change of the value of attr, or NULL. */
	void (*changed)(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr);
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

/**
 * Called on the stack thread when a remote command, a write, a timer of the stack or the reset of
 * the node gives attribute attr of a cluster on endpoint ep a new value. The new value is in the
 * value struct of the cluster.
 */
typedef void (*zzb_zcl_cb_t)(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr);

/**
 * @brief Set an attribute of a server cluster on an endpoint.
 *
 * Runs on the stack thread. value points to a value of the type of the field: uint8_t or bool for
 * the one-octet types, uint16_t for ZZB_ZCL_UINT16. A non-volatile attribute is saved. zcl_cb is
 * not called.
 *
 * @retval 0 on success.
 * @retval -ENOENT the node has no server cluster with the attribute on the endpoint.
 * @retval -EINVAL the attribute is a constant or a string, or the value is out of range.
 */
int zzb_zcl_attr_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		     const void *value);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_H_ */
