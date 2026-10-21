/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Time cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_TIME_H_
#define ZZB_INCLUDE_ZZB_ZCL_TIME_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Time cluster (ZCL 3.12). */
#define ZZB_ZCL_TIME 0x000a

/** Attributes (ZCL Table 3-69). @{ */
#define ZZB_ZCL_TIME_TIME             0x0000
#define ZZB_ZCL_TIME_STATUS           0x0001
#define ZZB_ZCL_TIME_ZONE             0x0002
#define ZZB_ZCL_TIME_DST_START        0x0003
#define ZZB_ZCL_TIME_DST_END          0x0004
#define ZZB_ZCL_TIME_DST_SHIFT        0x0005
#define ZZB_ZCL_TIME_STANDARD_TIME    0x0006
#define ZZB_ZCL_TIME_LOCAL_TIME       0x0007
#define ZZB_ZCL_TIME_LAST_SET_TIME    0x0008
#define ZZB_ZCL_TIME_VALID_UNTIL_TIME 0x0009
/** @} */

/** Bits of TimeStatus (ZCL Table 3-70). @{ */
#define ZZB_ZCL_TIME_MASTER          BIT(0)
#define ZZB_ZCL_TIME_SYNCHRONIZED    BIT(1)
#define ZZB_ZCL_TIME_MASTER_ZONE_DST BIT(2)
#define ZZB_ZCL_TIME_SUPERSEDING     BIT(3)
/** @} */

/** Time and the other time attributes that are not valid (ZCL 2.6.2.24, 3.12.2.2). */
#define ZZB_ZCL_TIME_UNKNOWN 0xffffffff

/**
 * Values of a Time server (ZCL 3.12.2.2). Times are seconds since 2000-01-01 00:00 UTC. The stack
 * writes time, standard_time and local_time from the clock of the node before they are read; they
 * read ZZB_ZCL_TIME_UNKNOWN without time. A write of time and zzb_zcl_attr_set() of it set the
 * clock and last_set_time; a remote write gets READ_ONLY while ZZB_ZCL_TIME_MASTER is set. The
 * application fills time_status before zzb_node_init() and changes it with zzb_zcl_attr_set(); a
 * remote write changes only ZZB_ZCL_TIME_SYNCHRONIZED, which stays 0 while ZZB_ZCL_TIME_MASTER is
 * set, and ZZB_ZCL_TIME_SUPERSEDING. time_zone, dst_start, dst_end, dst_shift and valid_until_time
 * are writable and saved; a remote write of the first four gets READ_ONLY while
 * ZZB_ZCL_TIME_MASTER_ZONE_DST is set.
 */
struct zzb_zcl_time {
	/** UTC time, 0x00000000 to 0xfffffffe, or ZZB_ZCL_TIME_UNKNOWN. */
	uint32_t time;
	/** TimeStatus bits, ZZB_ZCL_TIME_MASTER to ZZB_ZCL_TIME_SUPERSEDING. */
	uint8_t time_status;
	/** Seconds added to time for standard_time, -86400 to 86400; default 0. */
	int32_t time_zone;
	/** UTC times of the start and the end of DST; default ZZB_ZCL_TIME_UNKNOWN. */
	uint32_t dst_start;
	uint32_t dst_end;
	/** Seconds added to standard_time from dst_start to dst_end, -86400 to 86400; default 0. */
	int32_t dst_shift;
	uint32_t standard_time;
	uint32_t local_time;
	/** The value that time was set to last; ZZB_ZCL_TIME_UNKNOWN after the start. */
	uint32_t last_set_time;
	/** UTC time up to which time may be trusted; default ZZB_ZCL_TIME_UNKNOWN. */
	uint32_t valid_until_time;
	/** Internal: the bits of time_status that a remote write does not change. */
	uint8_t fixed_status;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_time_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_time_client_def;

/** A Time server with the values v, a pointer to struct zzb_zcl_time. */
#define ZZB_ZCL_TIME_SERVER(v)                                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_TIME,                                                                \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_time *)NULL),                                 \
		.def = &zzb_zcl_time_def,                                                          \
	}

/**
 * A Time client. The first endpoint with one sets the clock of the node from a Time server: it
 * reads Time, TimeStatus, TimeZone and LocalTime from its bound destinations, or without a binding
 * from endpoint 1 of the coordinator, at the start, 60 s after a request without a valid response
 * or after the reset of the node, and 24 h after a valid response.
 */
#define ZZB_ZCL_TIME_CLIENT()                                                                      \
	{                                                                                          \
		.id = ZZB_ZCL_TIME,                                                                \
		.server = false,                                                                   \
		.def = &zzb_zcl_time_client_def,                                                   \
	}

/**
 * @brief Get the time of the node.
 *
 * Runs on the stack thread. Times are seconds since 2000-01-01 00:00 UTC (ZCL 2.6.2.24). The local
 * time is LocalTime of the Time server on the first endpoint that has one, otherwise the UTC time
 * plus the offset of the last setting of the clock.
 *
 * @retval 0 on success.
 * @retval -ENODATA the clock of the node has not been set since the start or the reset of the node.
 */
int zzb_zcl_time_get(struct zzb_node *node, uint32_t *utc, uint32_t *local);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_TIME_H_ */
