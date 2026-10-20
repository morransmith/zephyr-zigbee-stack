/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_POLL_CONTROL_H_
#define ZZB_INCLUDE_ZZB_ZCL_POLL_CONTROL_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Poll Control cluster (ZCL 3.16). */
#define ZZB_ZCL_POLL_CONTROL 0x0020

/** Attributes (ZCL Table 3-132). */
#define ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL      0x0000
#define ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL     0x0001
#define ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL    0x0002
#define ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT      0x0003
#define ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL_MIN  0x0004
#define ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL_MIN 0x0005
#define ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT_MAX  0x0006

/** Command that the server generates (ZCL Table 3-133). */
#define ZZB_ZCL_POLL_CONTROL_CHECK_IN 0x00

/** Commands that the server receives (ZCL Table 3-134). */
#define ZZB_ZCL_POLL_CONTROL_CHECK_IN_RSP            0x00
#define ZZB_ZCL_POLL_CONTROL_FAST_POLL_STOP          0x01
#define ZZB_ZCL_POLL_CONTROL_SET_LONG_POLL_INTERVAL  0x02
#define ZZB_ZCL_POLL_CONTROL_SET_SHORT_POLL_INTERVAL 0x03

/**
 * Values of a Poll Control server (ZCL 3.16.4) of a sleepy end device; all intervals and timeouts
 * are in quarter seconds. The application fills the three limits before zzb_node_init() and
 * changes them with zzb_zcl_attr_set(); they are read-only, and 0 means no limit. The stack keeps
 * and saves the other four: check_in_interval and fast_poll_timeout are writable, and Set Long
 * Poll Interval, Set Short Poll Interval and zzb_zcl_attr_set() change the poll intervals. The
 * server on the first endpoint with one sets the poll intervals of the node. Every server sends
 * Check-in to its bound destinations every check_in_interval.
 */
struct zzb_zcl_poll_control {
	/** Up to 0x6e0000, 0 turns the check-ins off; default 0x3840 (1 h). */
	uint32_t check_in_interval;
	/** 0x04 to 0x6e0000; default CONFIG_ZZB_NWK_LONG_POLL_MS / 250. */
	uint32_t long_poll_interval;
	/** 0x0001 to 0xffff; default CONFIG_ZZB_NWK_SHORT_POLL_MS / 250. */
	uint16_t short_poll_interval;
	/** 0x0001 to 0xffff; default 0x28 (10 s). */
	uint16_t fast_poll_timeout;
	uint32_t check_in_interval_min;
	uint32_t long_poll_interval_min;
	/** zigpy writes fast_poll_timeout 120, so 0 or at least 120 for ZHA. */
	uint16_t fast_poll_timeout_max;
	/** Internal: k_uptime_get() of the next check-in, 0 without check-ins. */
	int64_t next_check_in;
	/** Internal: k_uptime_get() at the end of the window after a check-in, 0 outside it. */
	int64_t window_end;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_poll_control_def;

/** A Poll Control server with the values v, a pointer to struct zzb_zcl_poll_control. */
#define ZZB_ZCL_POLL_CONTROL_SERVER(v)                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_POLL_CONTROL,                                                        \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_poll_control *)NULL),                         \
		.def = &zzb_zcl_poll_control_def,                                                  \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_POLL_CONTROL_H_ */
