/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zzb/zcl/time.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* Time and UTCTime: not a value (ZCL 2.6.2.24) */
#define NO_TIME 0xffffffff

/* ClusterRevision (ZCL 3.12.1.1) */
#define REVISION 2

/* The range of TimeZone and DstShift in seconds (ZCL Table 3-69) */
#define SHIFT_MAX 86400

/* The bits of TimeStatus that a remote write changes (ZCL 3.12.2.2.2) */
#define WRITABLE_STATUS (ZZB_ZCL_TIME_SYNCHRONIZED | ZZB_ZCL_TIME_SUPERSEDING)

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_TIME, ZZB_ZCL_UTC, ZZB_ZCL_WRITE, struct zzb_zcl_time, time),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_STATUS, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE, struct zzb_zcl_time,
		     time_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_ZONE, ZZB_ZCL_INT32, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_time, time_zone),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_DST_START, ZZB_ZCL_UINT32, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_time, dst_start),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_DST_END, ZZB_ZCL_UINT32, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_time, dst_end),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_DST_SHIFT, ZZB_ZCL_INT32, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_time, dst_shift),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_STANDARD_TIME, ZZB_ZCL_UINT32, 0, struct zzb_zcl_time,
		     standard_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_LOCAL_TIME, ZZB_ZCL_UINT32, 0, struct zzb_zcl_time, local_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_LAST_SET_TIME, ZZB_ZCL_UTC, 0, struct zzb_zcl_time,
		     last_set_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_TIME_VALID_UNTIL_TIME, ZZB_ZCL_UTC, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_time, valid_until_time),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* StandardTime of v at the UTC time utc (ZCL 3.12.2.2.3) */
static uint32_t standard_time(const struct zzb_zcl_time *v, uint32_t utc)
{
	return utc == NO_TIME ? NO_TIME : utc + v->time_zone;
}

/* LocalTime of v at the UTC time utc (ZCL 3.12.2.2.4) */
static uint32_t local_time(const struct zzb_zcl_time *v, uint32_t utc)
{
	uint32_t standard = standard_time(v, utc);

	if (utc == NO_TIME || utc < v->dst_start || utc > v->dst_end) {
		return standard;
	}

	return standard + v->dst_shift;
}

static void refresh(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_time *v = values;

	v->time = zzb_zcl_clock_utc(node);
	v->standard_time = standard_time(v, v->time);
	v->local_time = local_time(v, v->time);
}

/* READ_ONLY while bit is set in TimeStatus */
static uint8_t locked(const struct zzb_zcl_time *v, uint8_t bit)
{
	return (v->time_status & bit) ? ZZB_ZCL_READ_ONLY : ZZB_ZCL_SUCCESS;
}

/* ZCL 3.12.2.2.1 to 3.12.2.2.6 */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	const struct zzb_zcl_time *v = values;
	int32_t shift = value;

	switch (attr->id) {
	case ZZB_ZCL_TIME_TIME:
		if (value == NO_TIME) {
			return ZZB_ZCL_INVALID_VALUE;
		}

		return locked(v, ZZB_ZCL_TIME_MASTER);
	case ZZB_ZCL_TIME_ZONE:
	case ZZB_ZCL_TIME_DST_SHIFT:
		if (shift < -SHIFT_MAX || shift > SHIFT_MAX) {
			return ZZB_ZCL_INVALID_VALUE;
		}

		return locked(v, ZZB_ZCL_TIME_MASTER_ZONE_DST);
	case ZZB_ZCL_TIME_DST_START:
	case ZZB_ZCL_TIME_DST_END:
		return locked(v, ZZB_ZCL_TIME_MASTER_ZONE_DST);
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

/* The clock keeps its offset, and LastSetTime copies Time (ZCL 3.12.2.2.9) */
static void time_changed(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_time *v)
{
	zzb_zcl_clock_set(node, v->time, node->zcl.clock.offset);
	zzb_zcl_set(node, ep, ZZB_ZCL_TIME, ZZB_ZCL_TIME_LAST_SET_TIME, v->time);
}

/* The application sets every bit, a remote write Synchronized and Superseding (ZCL 3.12.2.2.2) */
static void status_changed(struct zzb_node *node, struct zzb_zcl_time *v)
{
	if (node->zcl.received == NULL) {
		v->fixed_status = v->time_status & ~WRITABLE_STATUS;
	}

	v->time_status = (v->time_status & WRITABLE_STATUS) | v->fixed_status;
	if (v->time_status & ZZB_ZCL_TIME_MASTER) {
		v->time_status &= ~ZZB_ZCL_TIME_SYNCHRONIZED;
	}
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	switch (attr) {
	case ZZB_ZCL_TIME_TIME:
		time_changed(node, ep, values);
		break;
	case ZZB_ZCL_TIME_STATUS:
		status_changed(node, values);
		break;
	}
}

/* The defaults of ZCL Table 3-69; TimeStatus keeps the value of the application */
static void init(void *values)
{
	struct zzb_zcl_time *v = values;

	v->time = NO_TIME;
	v->time_zone = 0;
	v->dst_start = NO_TIME;
	v->dst_end = NO_TIME;
	v->dst_shift = 0;
	v->standard_time = NO_TIME;
	v->local_time = NO_TIME;
	v->last_set_time = NO_TIME;
	v->valid_until_time = NO_TIME;
	v->fixed_status = v->time_status & ~WRITABLE_STATUS;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_TIME, attr, value);
}

/* zzb_zcl_reset() clears the clock after the reset hooks */
static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_TIME_STATUS, 0);
	set(node, ep, ZZB_ZCL_TIME_ZONE, 0);
	set(node, ep, ZZB_ZCL_TIME_DST_START, NO_TIME);
	set(node, ep, ZZB_ZCL_TIME_DST_END, NO_TIME);
	set(node, ep, ZZB_ZCL_TIME_DST_SHIFT, 0);
	set(node, ep, ZZB_ZCL_TIME_LAST_SET_TIME, NO_TIME);
	set(node, ep, ZZB_ZCL_TIME_VALID_UNTIL_TIME, NO_TIME);
}

const struct zzb_zcl_cluster_def zzb_zcl_time_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
	.changed = changed,
	.refresh = refresh,
};

void zzb_zcl_clock_set(struct zzb_node *node, uint32_t utc, int32_t offset)
{
	node->zcl.clock = (struct zzb_zcl_clock){
		.set = true,
		.utc = utc,
		.uptime = k_uptime_get(),
		.offset = offset,
	};
}

uint32_t zzb_zcl_clock_utc(const struct zzb_node *node)
{
	const struct zzb_zcl_clock *c = &node->zcl.clock;

	if (!c->set) {
		return NO_TIME;
	}

	return c->utc + (k_uptime_get() - c->uptime) / MSEC_PER_SEC;
}

uint32_t zzb_zcl_clock_local(const struct zzb_node *node)
{
	uint32_t utc = zzb_zcl_clock_utc(node);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_cluster *cl =
			zzb_zcl_find_cluster(&node->device->endpoints[i], ZZB_ZCL_TIME, true);

		if (cl != NULL) {
			return local_time(cl->values, utc);
		}
	}

	return utc == NO_TIME ? NO_TIME : utc + node->zcl.clock.offset;
}

struct time_req {
	struct k_work work;
	struct zzb_node *node;
	uint32_t utc;
	uint32_t local;
};

static void time_handler(struct k_work *work)
{
	struct time_req *req = CONTAINER_OF(work, struct time_req, work);

	req->utc = zzb_zcl_clock_utc(req->node);
	req->local = zzb_zcl_clock_local(req->node);
}

int zzb_zcl_time_get(struct zzb_node *node, uint32_t *utc, uint32_t *local)
{
	struct time_req req = {
		.node = node,
	};

	zzb_call_sync(&req.work, time_handler);
	if (req.utc == NO_TIME) {
		return -ENODATA;
	}

	*utc = req.utc;
	*local = req.local;
	return 0;
}
