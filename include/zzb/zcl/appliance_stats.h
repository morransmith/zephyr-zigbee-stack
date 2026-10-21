/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief EN50523 Appliance Statistics cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_STATS_H_
#define ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_STATS_H_

#include <errno.h>
#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EN50523 Appliance Statistics cluster (ZCL 15.5). */
#define ZZB_ZCL_APPLIANCE_STATS 0x0b03

/** Attributes (ZCL Table 15-22). @{ */
#define ZZB_ZCL_APPLIANCE_STATS_LOG_MAX_SIZE       0x0000
#define ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_MAX_SIZE 0x0001
/** @} */

/** Commands that the server receives and a client sends (ZCL Table 15-24). @{ */
#define ZZB_ZCL_APPLIANCE_STATS_LOG_REQUEST       0x00
#define ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_REQUEST 0x01
/** @} */

/** Commands that the server generates and a client receives (ZCL Table 15-23). @{ */
#define ZZB_ZCL_APPLIANCE_STATS_LOG_NOTIFICATION     0x00
#define ZZB_ZCL_APPLIANCE_STATS_LOG_RSP              0x01
#define ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_RSP        0x02
#define ZZB_ZCL_APPLIANCE_STATS_STATISTICS_AVAILABLE 0x03
/** @} */

/** Time Stamp of a log of a server without the time (ZCL 15.5.2.2.1.2). */
#define ZZB_ZCL_APPLIANCE_STATS_TIME_STAMP_UNKNOWN 0xffffffff

/**
 * Values of an Appliance Statistics server (ZCL 15.5.2.1). The application fills them before
 * zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only.
 */
struct zzb_zcl_appliance_stats {
	/** The longest log payload in octets; ZCL default 0x3c. */
	uint32_t log_max_size;
	/** The number of logs the server keeps; ZCL default 1. */
	uint8_t log_queue_max_size;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_stats_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_stats_client_def;

/**
 * An Appliance Statistics server with the values v, a pointer to struct zzb_zcl_appliance_stats,
 * which the White Goods device may have (DTL). Log Request and Log Queue Request go to zcl_cmd_cb
 * after the length checks. The application keeps the logs, answers the requests with
 * zzb_zcl_cmd_respond() and sends Log Notification and Statistics Available with
 * zzb_zcl_send_bound().
 */
#define ZZB_ZCL_APPLIANCE_STATS_SERVER(v)                                                          \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_STATS,                                                     \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_appliance_stats *)NULL),                      \
		.def = &zzb_zcl_appliance_stats_def,                                               \
	}

/**
 * An Appliance Statistics client, which the Home Gateway has (DTL). The commands of the server go
 * to zcl_cmd_cb after the length checks, Log Notification and Log Response with the Log Length
 * octets of their log payload.
 */
#define ZZB_ZCL_APPLIANCE_STATS_CLIENT()                                                           \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_STATS,                                                     \
		.server = false,                                                                   \
		.def = &zzb_zcl_appliance_stats_client_def,                                        \
	}

/** The most log IDs of a Log Queue Response in the ASDU of 82 octets. */
#define ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX 19

/**
 * @brief Answer Log Queue Request within zcl_cmd_cb with Log Queue Response (ZCL 15.5.2.2.3).
 *
 * Sends the count log IDs of the logs that the server keeps.
 *
 * @return the result of zzb_zcl_cmd_respond(), or -EMSGSIZE for more than
 * ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX log IDs.
 */
static inline int zzb_zcl_appliance_stats_log_queue_rsp(struct zzb_node *node,
							const struct zzb_zcl_cmd *cmd,
							const uint32_t *log_ids, size_t count)
{
	/* Log Queue Size and 4 octets per log ID */
	uint8_t payload[1 + ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX * 4];
	uint8_t *p = payload;

	if (count > ZZB_ZCL_APPLIANCE_STATS_LOG_IDS_MAX) {
		return -EMSGSIZE;
	}

	*p++ = count;
	for (size_t i = 0; i < count; i++) {
		sys_put_le32(log_ids[i], p);
		p += 4;
	}

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_RSP, payload,
				   p - payload);
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_STATS_H_ */
