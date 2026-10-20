/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/poll_control.h>

#include "common/node.h"
#include "nwk/nwk.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.16.1.1) */
#define REVISION 3

#define QS_MS 250

/* Defaults (ZCL Table 3-132); the poll intervals are those of NWK */
#define CHECK_IN_INTERVAL_DEFAULT 0x3840
#define LONG_POLL_DEFAULT         (CONFIG_ZZB_NWK_LONG_POLL_MS / QS_MS)
#define SHORT_POLL_DEFAULT        (CONFIG_ZZB_NWK_SHORT_POLL_MS / QS_MS)
#define FAST_POLL_TIMEOUT_DEFAULT 0x28

/* The range of Check-inInterval and LongPollInterval (ZCL Table 3-132) */
#define INTERVAL_MAX  0x6e0000
#define LONG_POLL_MIN 0x04

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL, ZZB_ZCL_UINT32,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_poll_control, check_in_interval),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL, ZZB_ZCL_UINT32, ZZB_ZCL_NV,
		     struct zzb_zcl_poll_control, long_poll_interval),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL, ZZB_ZCL_UINT16, ZZB_ZCL_NV,
		     struct zzb_zcl_poll_control, short_poll_interval),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_poll_control, fast_poll_timeout),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL_MIN, ZZB_ZCL_UINT32, 0,
		     struct zzb_zcl_poll_control, check_in_interval_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL_MIN, ZZB_ZCL_UINT32, 0,
		     struct zzb_zcl_poll_control, long_poll_interval_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT_MAX, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_poll_control, fast_poll_timeout_max),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static uint8_t valid(bool cond)
{
	return cond ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

/* A FastPollTimeoutMax of 0 is no limit */
static bool above_max(const struct zzb_zcl_poll_control *v, uint32_t timeout)
{
	return v->fast_poll_timeout_max != 0 && timeout > v->fast_poll_timeout_max;
}

/* ZCL 3.16.4.1, 3.16.4.2; a minimum of 0 is no limit, and Check-inInterval 0 is always valid */
static uint8_t check_value(const struct zzb_zcl_poll_control *v, uint16_t id, uint32_t value)
{
	switch (id) {
	case ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL:
		return valid(value == 0 ||
			     (value <= INTERVAL_MAX && value >= v->check_in_interval_min &&
			      value >= v->long_poll_interval));
	case ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL:
		return valid(IN_RANGE(value, LONG_POLL_MIN, INTERVAL_MAX) &&
			     value >= v->long_poll_interval_min &&
			     value >= v->short_poll_interval &&
			     (v->check_in_interval == 0 || value <= v->check_in_interval));
	case ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL:
		return valid(value != 0 && value <= v->long_poll_interval);
	case ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT:
		return valid(value != 0 && !above_max(v, value));
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	return check_value(values, attr->id, value);
}

/* Whether ep is the first endpoint with a Poll Control server */
static bool first(const struct zzb_node *node, uint8_t ep)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *e = &node->device->endpoints[i];

		if (zzb_zcl_find_cluster(e, ZZB_ZCL_POLL_CONTROL, true) != NULL) {
			return e->id == ep;
		}
	}

	return false;
}

/* The server on the first endpoint sets the poll intervals of NWK */
static void set_intervals(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_poll_control *v)
{
	if (first(node, ep)) {
		zzb_nwk_set_poll_intervals(node, v->long_poll_interval * QS_MS,
					   v->short_poll_interval * QS_MS);
	}
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	if (attr == ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL ||
	    attr == ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL) {
		set_intervals(node, ep, values);
	}
}

/* Set Long Poll Interval and Set Short Poll Interval with a field of len octets */
static int set_interval(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			const struct zzb_zcl_poll_control *v, uint16_t attr, size_t len)
{
	uint32_t value;

	if (cmd->len < len) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	value = len == sizeof(uint32_t) ? sys_get_le32(cmd->payload) : sys_get_le16(cmd->payload);
	if (check_value(v, attr, value) != ZZB_ZCL_SUCCESS) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	zzb_zcl_set(node, cmd->ep, ZZB_ZCL_POLL_CONTROL, attr, value);
	return ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_POLL_CONTROL_SET_LONG_POLL_INTERVAL:
		return set_interval(node, cmd, values, ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL,
				    sizeof(uint32_t));
	case ZZB_ZCL_POLL_CONTROL_SET_SHORT_POLL_INTERVAL:
		return set_interval(node, cmd, values, ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL,
				    sizeof(uint16_t));
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* The limits of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_poll_control *v = values;

	v->check_in_interval = CHECK_IN_INTERVAL_DEFAULT;
	v->long_poll_interval = LONG_POLL_DEFAULT;
	v->short_poll_interval = SHORT_POLL_DEFAULT;
	v->fast_poll_timeout = FAST_POLL_TIMEOUT_DEFAULT;
}

static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	set_intervals(node, ep, values);
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_POLL_CONTROL, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL, CHECK_IN_INTERVAL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL, LONG_POLL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL, SHORT_POLL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT, FAST_POLL_TIMEOUT_DEFAULT);
	set_intervals(node, ep, values);
}

const struct zzb_zcl_cluster_def zzb_zcl_poll_control_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.cmd = cmd,
	.check = check,
	.changed = changed,
};
