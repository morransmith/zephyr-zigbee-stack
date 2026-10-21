/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/time.h>

#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.12.1.1) */
#define REVISION 2

/* The range of TimeZone and DstShift in seconds (ZCL Table 3-69) */
#define SHIFT_MAX 86400

/* The bits of TimeStatus that a remote write changes (ZCL 3.12.2.2.2) */
#define WRITABLE_STATUS (ZZB_ZCL_TIME_SYNCHRONIZED | ZZB_ZCL_TIME_SUPERSEDING)

/* The Time server of a coordinator with zigpy-znp */
#define COORDINATOR    0x0000
#define COORDINATOR_EP 1

/* The next request of the Time client without and with a valid response */
#define RETRY  K_SECONDS(60)
#define RESYNC K_HOURS(24)

/* Read Attributes of Time, TimeStatus, TimeZone and LocalTime */
static const uint8_t read_req[] = {0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00};

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
	return utc == ZZB_ZCL_TIME_UNKNOWN ? ZZB_ZCL_TIME_UNKNOWN : utc + v->time_zone;
}

/* LocalTime of v at the UTC time utc (ZCL 3.12.2.2.4) */
static uint32_t local_time(const struct zzb_zcl_time *v, uint32_t utc)
{
	uint32_t standard = standard_time(v, utc);

	if (utc == ZZB_ZCL_TIME_UNKNOWN || utc < v->dst_start || utc > v->dst_end) {
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
		if (value == ZZB_ZCL_TIME_UNKNOWN) {
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

	v->time = ZZB_ZCL_TIME_UNKNOWN;
	v->time_zone = 0;
	v->dst_start = ZZB_ZCL_TIME_UNKNOWN;
	v->dst_end = ZZB_ZCL_TIME_UNKNOWN;
	v->dst_shift = 0;
	v->standard_time = ZZB_ZCL_TIME_UNKNOWN;
	v->local_time = ZZB_ZCL_TIME_UNKNOWN;
	v->last_set_time = ZZB_ZCL_TIME_UNKNOWN;
	v->valid_until_time = ZZB_ZCL_TIME_UNKNOWN;
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
	set(node, ep, ZZB_ZCL_TIME_DST_START, ZZB_ZCL_TIME_UNKNOWN);
	set(node, ep, ZZB_ZCL_TIME_DST_END, ZZB_ZCL_TIME_UNKNOWN);
	set(node, ep, ZZB_ZCL_TIME_DST_SHIFT, 0);
	set(node, ep, ZZB_ZCL_TIME_LAST_SET_TIME, ZZB_ZCL_TIME_UNKNOWN);
	set(node, ep, ZZB_ZCL_TIME_VALID_UNTIL_TIME, ZZB_ZCL_TIME_UNKNOWN);
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

/* The first endpoint with a Time client, or NULL */
static const struct zzb_endpoint *client(const struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		if (zzb_zcl_find_cluster(ep, ZZB_ZCL_TIME, false) != NULL) {
			return ep;
		}
	}

	return NULL;
}

/*
 * Sends Read Attributes from the Time client on ep to its bound destinations, or without a binding
 * to the coordinator. A sleepy node polls fast for the response.
 */
static void request(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_hdr hdr = {
		.type = ZZB_ZCL_FRAME_GLOBAL,
		.flags = ZZB_ZCL_DISABLE_DEFAULT_RSP,
		.tsn = node->zcl.tsn,
		.cmd = ZZB_ZCL_READ_ATTRIBUTES,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = COORDINATOR,
		.dst_ep = COORDINATOR_EP,
		.profile = ep->profile,
		.cluster = ZZB_ZCL_TIME,
		.src_ep = ep->id,
		.tx_options = ZZB_APS_TX_ACK,
	};
	int err;

	/* zzb_zcl_send_bindings() sends with the same TSN and advances the counter */
	node->zcl.time_tsn = hdr.tsn;
	err = zzb_zcl_send_bindings(node, ep, ZZB_ZCL_TIME, ZZB_ZCL_FRAME_GLOBAL, false,
				    ZZB_ZCL_READ_ATTRIBUTES, read_req, sizeof(read_req));
	if (err == -ENOENT) {
		err = zzb_zcl_send(node, &aps, &hdr, read_req, sizeof(read_req));
	}

	if (err == 0) {
		zzb_nwk_poll_fast(node);
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.time_timer, RETRY);
}

/* Armed only on a node with a Time client */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.time_timer);

	request(node, client(node));
}

/* The first endpoint with a Time client sends the requests */
static void client_start(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_endpoint *first = client(node);

	if (first->id == ep) {
		request(node, first);
	}
}

/* The reset of the node clears the clock, so the requests start again */
static void client_reset(struct zzb_node *node, uint8_t ep, void *values)
{
	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.time_timer, RETRY);
}

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_time_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.start = client_start,
	.reset = client_reset,
	.cmd = zzb_zcl_client_cmd,
};

void zzb_zcl_time_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.time_timer, tick);
}

/*
 * Puts the value of attribute id from its record with SUCCESS and type in a Read Attributes
 * Response (ZCL 2.5.2) in *value; false without one
 */
static bool find(const struct zzb_zcl_cmd *cmd, uint16_t id, uint8_t type, uint32_t *value)
{
	const uint8_t *p = cmd->payload;
	size_t left = cmd->len;

	while (left >= 3) {
		uint16_t attr = sys_get_le16(p);
		int len;

		/* A record with an error has no value */
		if (p[2] != ZZB_ZCL_SUCCESS) {
			p += 3;
			left -= 3;
			continue;
		}

		len = left > 3 ? zzb_zcl_value_len(p[3], p + 4, left - 4) : -EINVAL;
		if (len < 0) {
			return false;
		}

		if (attr == id && p[3] == type) {
			*value = zzb_zcl_value_get(type, p + 4);
			return true;
		}

		p += 4 + len;
		left -= 4 + len;
	}

	return false;
}

int zzb_zcl_time_read_rsp(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	const struct zzb_zcl_cmd *cmd = &req->cmd;
	uint32_t utc;
	uint32_t status;
	uint32_t local;
	uint32_t offset = 0;

	if (req->cluster->server || req->cluster->id != ZZB_ZCL_TIME) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	/* ZCL 3.12.2.2.2: a server without Master and Synchronized has no defined time */
	if (cmd->tsn != node->zcl.time_tsn || !find(cmd, ZZB_ZCL_TIME_TIME, ZZB_ZCL_UTC, &utc) ||
	    utc == ZZB_ZCL_TIME_UNKNOWN || !find(cmd, ZZB_ZCL_TIME_STATUS, ZZB_ZCL_MAP8, &status) ||
	    !(status & (ZZB_ZCL_TIME_MASTER | ZZB_ZCL_TIME_SYNCHRONIZED))) {
		return ZZB_ZCL_SUCCESS;
	}

	/* Without LocalTime and TimeZone the offset stays 0 */
	if (find(cmd, ZZB_ZCL_TIME_LOCAL_TIME, ZZB_ZCL_UINT32, &local)) {
		offset = local - utc;
	} else {
		find(cmd, ZZB_ZCL_TIME_ZONE, ZZB_ZCL_INT32, &offset);
	}

	zzb_zcl_clock_set(node, utc, offset);
	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.time_timer, RESYNC);
	return ZZB_ZCL_SUCCESS;
}

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
		return ZZB_ZCL_TIME_UNKNOWN;
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

	return utc == ZZB_ZCL_TIME_UNKNOWN ? ZZB_ZCL_TIME_UNKNOWN : utc + node->zcl.clock.offset;
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
	if (req.utc == ZZB_ZCL_TIME_UNKNOWN) {
		return -ENODATA;
	}

	*utc = req.utc;
	*local = req.local;
	return 0;
}
