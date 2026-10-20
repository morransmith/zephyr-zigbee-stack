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
	ZZB_ZCL_MAP16 = 0x19,
	ZZB_ZCL_MAP32 = 0x1b,
	ZZB_ZCL_UINT8 = 0x20,
	ZZB_ZCL_UINT16 = 0x21,
	ZZB_ZCL_UINT24 = 0x22,
	ZZB_ZCL_UINT32 = 0x23,
	ZZB_ZCL_UINT48 = 0x25,
	ZZB_ZCL_UINT56 = 0x26,
	ZZB_ZCL_INT8 = 0x28,
	ZZB_ZCL_INT16 = 0x29,
	ZZB_ZCL_INT24 = 0x2a,
	ZZB_ZCL_INT32 = 0x2b,
	ZZB_ZCL_ENUM8 = 0x30,
	ZZB_ZCL_ENUM16 = 0x31,
	ZZB_ZCL_OCTSTR = 0x41,
	ZZB_ZCL_STRING = 0x42,
	ZZB_ZCL_UTC = 0xe2,
	ZZB_ZCL_EUI64 = 0xf0,
};

#define ZZB_ZCL_IS_STRING(type) ((type) == ZZB_ZCL_OCTSTR || (type) == ZZB_ZCL_STRING)
#define ZZB_ZCL_IS_BYTE(type)                                                                      \
	((type) == ZZB_ZCL_BOOL || (type) == ZZB_ZCL_MAP8 || (type) == ZZB_ZCL_UINT8 ||            \
	 (type) == ZZB_ZCL_INT8 || (type) == ZZB_ZCL_ENUM8)

#define ZZB_ZCL_IS_WORD(type)                                                                      \
	((type) == ZZB_ZCL_MAP16 || (type) == ZZB_ZCL_UINT16 || (type) == ZZB_ZCL_INT16 ||         \
	 (type) == ZZB_ZCL_ENUM16)

#define ZZB_ZCL_IS_DWORD(type)                                                                     \
	((type) == ZZB_ZCL_MAP32 || (type) == ZZB_ZCL_UINT24 || (type) == ZZB_ZCL_UINT32 ||        \
	 (type) == ZZB_ZCL_INT24 || (type) == ZZB_ZCL_INT32 || (type) == ZZB_ZCL_UTC)

#define ZZB_ZCL_IS_QWORD(type)                                                                     \
	((type) == ZZB_ZCL_UINT48 || (type) == ZZB_ZCL_UINT56 || (type) == ZZB_ZCL_EUI64)

/**
 * Size of the value struct field of a type; 0 for a type that the stack does not store. A uint24
 * and an int24 have a field of 4 octets, a uint48 and a uint56 a field of 8.
 */
#define ZZB_ZCL_STORAGE_SIZE(type)                                                                 \
	(ZZB_ZCL_IS_QWORD(type)    ? 8                                                             \
	 : ZZB_ZCL_IS_DWORD(type)  ? 4                                                             \
	 : ZZB_ZCL_IS_WORD(type)   ? 2                                                             \
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
 * uint8_t pointer to its length octet; both are read-only, and NULL reads as an empty string. An
 * int24 is stored sign-extended in an int32_t. A uint48, uint56 or EUI64 is stored as a uint64_t;
 * it is not in a scene extension field set, and a write of it does not call the check hook. An
 * EUI64 is not reportable.
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
	ZZB_ZCL_FAILURE = 0x01,
	ZZB_ZCL_NOT_AUTHORIZED = 0x7e,
	ZZB_ZCL_MALFORMED_COMMAND = 0x80,
	ZZB_ZCL_UNSUP_COMMAND = 0x81,
	ZZB_ZCL_INVALID_FIELD = 0x85,
	ZZB_ZCL_UNSUPPORTED_ATTRIBUTE = 0x86,
	ZZB_ZCL_INVALID_VALUE = 0x87,
	ZZB_ZCL_READ_ONLY = 0x88,
	ZZB_ZCL_INSUFFICIENT_SPACE = 0x89,
	ZZB_ZCL_NOT_FOUND = 0x8b,
	ZZB_ZCL_UNREPORTABLE_ATTRIBUTE = 0x8c,
	ZZB_ZCL_INVALID_DATA_TYPE = 0x8d,
	ZZB_ZCL_ABORT = 0x95,
	ZZB_ZCL_INVALID_IMAGE = 0x96,
	ZZB_ZCL_WAIT_FOR_DATA = 0x97,
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
	/** The command came as a NWK unicast to the endpoint, which ZCL responses may answer. */
	bool unicast;
	/** The command addresses the server instance (direction 0), otherwise the client. */
	bool server;
};

/** An attribute of the scene extension field set of a cluster (ZCL 3.7.2.4.2). */
struct zzb_zcl_scene_attr {
	uint16_t id;
	uint8_t type;
};

/** The longest scene extension field set of a cluster, in attributes. */
#define ZZB_ZCL_SCENE_ATTRS_MAX 8

/** The attributes and commands of a cluster. */
struct zzb_zcl_cluster_def {
	/** Attributes in ascending order of ID. */
	const struct zzb_zcl_attr *attrs;
	uint8_t attr_count;
	/**
	 * The attributes of the scene extension field set in its order, at most
	 * ZZB_ZCL_SCENE_ATTRS_MAX, or NULL. An attribute that attrs does not have is stored as 0.
	 */
	const struct zzb_zcl_scene_attr *scene_attrs;
	uint8_t scene_attr_count;
	/** Sets the defaults before the settings records are loaded, or NULL. */
	void (*init)(void *values);
	/** Runs after the settings records are loaded, on the stack thread, or NULL. */
	void (*start)(struct zzb_node *node, uint8_t ep, void *values);
	/** Restores the defaults at the reset of the node, on the stack thread, or NULL. */
	void (*reset)(struct zzb_node *node, uint8_t ep, void *values);
	/**
	 * Processes a cluster-specific command on the stack thread, or NULL. Returns the status of
	 * the Default Response or ZZB_ZCL_RESPONDED.
	 */
	int (*cmd)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values);
	/**
	 * Checks a remote write of value to attr against the values before the write, or NULL.
	 * Returns ZZB_ZCL_SUCCESS, ZZB_ZCL_INVALID_VALUE, or ZZB_ZCL_READ_ONLY for an attribute
	 * that is writable only under a condition; zzb_zcl_attr_set() ignores READ_ONLY.
	 */
	uint8_t (*check)(const void *values, const struct zzb_zcl_attr *attr, uint32_t value);
	/** Called on the stack thread after every change of the value of attr, or NULL. */
	void (*changed)(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr);
	/**
	 * Applies the extension field set of a recalled scene on the stack thread, or NULL: count
	 * values in the order of scene_attrs, fewer when the set lacks trailing attributes, and the
	 * transition time in tenths of a second.
	 */
	void (*recall)(struct zzb_node *node, uint8_t ep, void *values, const uint32_t *set,
		       size_t count, uint32_t transition_ds);
	/**
	 * Raises the alarms of the cluster whose conditions last again (ZCL 3.11.2.4.1), on the
	 * stack thread, or NULL. code is the alarm code of Reset Alarm, or 0xff for Reset All
	 * Alarms.
	 */
	void (*alarm_reset)(struct zzb_node *node, uint8_t ep, void *values, uint8_t code);
	/**
	 * Updates the values that the cluster computes before Read Attributes reads them and
	 * before a write checks them, on the stack thread, or NULL. These attributes are not
	 * reportable, not non-volatile and not in the scene extension field set.
	 */
	void (*refresh)(struct zzb_node *node, uint8_t ep, void *values);
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
 * Internal: a transition of Level Control or Color Control, a field of the value struct. It moves
 * up to two attributes of at most 16 bits together.
 */
struct zzb_zcl_transition {
	/** k_uptime_get() at the start. */
	int64_t start;
	/** Milliseconds. */
	uint32_t duration;
	/** The number of attributes, 0 while no transition runs. */
	uint8_t count;
	uint16_t attr[2];
	uint16_t from[2];
	uint16_t to[2];
	/** The values written last. */
	uint16_t last[2];
};

/**
 * Called on the stack thread when a remote command, a write, a timer of the stack or the reset of
 * the node gives attribute attr of a cluster on endpoint ep a new value. The new value is in the
 * value struct of the cluster.
 */
typedef void (*zzb_zcl_cb_t)(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr);

/**
 * Called on the stack thread for Trigger Effect of an Identify server and Off With Effect of an
 * On/Off server, with the fields as received. For a variant that the application does not
 * support, it uses the default variant (ZCL 3.5.2.3.3.2, 3.8.2.3.4.2).
 */
typedef void (*zzb_zcl_effect_cb_t)(struct zzb_node *node, uint8_t ep, uint16_t cluster,
				    uint8_t effect, uint8_t variant);

/**
 * Called on the stack thread for a cluster-specific command that the application carries out, such
 * as a command of Window Covering, after the checks of the stack. The payload is valid during the
 * call. Returns the status of the Default Response, unless the application answers the command
 * with zzb_zcl_cmd_respond(). A recalled Window Covering scene comes as Go To Lift Percentage and
 * Go To Tilt Percentage, the end of an IAS WD warning as Start Warning with all fields 0 and the
 * relock of a Door Lock as Lock Door without a payload, with unicast false, src_addr, src_ep and
 * tsn 0; their status is ignored.
 */
typedef uint8_t (*zzb_zcl_cmd_cb_t)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd);

/**
 * Called on the stack thread after the OTA Upgrade client has requested a test upgrade with
 * boot_request_upgrade(). The application reboots, e.g. with sys_reboot(), and confirms the new
 * image with boot_write_img_confirmed() once it runs.
 */
typedef void (*zzb_zcl_ota_cb_t)(struct zzb_node *node);

/**
 * @brief Set an attribute of a server cluster on an endpoint.
 *
 * Runs on the stack thread. value points to a value of the type of the field: uint8_t, int8_t or
 * bool for the one-octet types, uint16_t or int16_t for the two-octet types, uint32_t for
 * ZZB_ZCL_UINT24, ZZB_ZCL_UINT32, ZZB_ZCL_MAP32 and ZZB_ZCL_UTC, int32_t for ZZB_ZCL_INT24 and
 * ZZB_ZCL_INT32, uint64_t for ZZB_ZCL_UINT48 and ZZB_ZCL_UINT56. A non-volatile attribute is
 * saved. zcl_cb is not called.
 *
 * @retval 0 on success.
 * @retval -ENOENT the node has no server cluster with the attribute on the endpoint.
 * @retval -EINVAL the attribute is a constant, a string or an EUI64, or the value is out of range.
 */
int zzb_zcl_attr_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		     const void *value);

/**
 * @brief Answer a command that zcl_cmd_cb receives.
 *
 * Runs on the stack thread within zcl_cmd_cb. Sends the cluster-specific command id with the
 * payload to the source of cmd, with the TSN of cmd, the opposite direction and Disable Default
 * Response 1. Then no Default Response follows, and the status that zcl_cmd_cb returns is ignored
 * (ZCL 2.5.12.2). After an error the Default Response carries that status.
 *
 * @retval 0 on success.
 * @retval -EINVAL cmd did not come as unicast, such as a command that the stack builds.
 * @retval -EMSGSIZE the frame is longer than the ASDU of 82 octets.
 * @retval -ENOBUFS no buffer or transmission table entry is free.
 */
int zzb_zcl_cmd_respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t id,
			const uint8_t *payload, size_t len);

/**
 * @brief Send a cluster-specific command of a cluster to the bound destinations.
 *
 * Runs on the stack thread. The command goes from the client instance of the cluster on the
 * endpoint, or from the server instance, with the direction to the client, when the endpoint has no
 * client instance, to the destinations of its bindings, with Disable Default Response 1. The
 * application builds the payload; no confirm follows.
 *
 * @retval 0 on success.
 * @retval -ENOENT the endpoint has no instance of the cluster, or the cluster no binding.
 * @retval -EMSGSIZE the frame is longer than the ASDU of 82 octets.
 * @retval -ENOBUFS no buffer or transmission table entry is free.
 * @retval -ENOTCONN the node is not on a network.
 * @retval -EBUSY the network layer runs a procedure, such as a rejoin.
 */
int zzb_zcl_send_bound(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t cmd,
		       const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_H_ */
