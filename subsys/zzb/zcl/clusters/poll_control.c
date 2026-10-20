/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/poll_control.h>

#include "common/node.h"
#include "common/queue.h"
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

/* The server fast polls for the responses after a check-in (ZCL 3.16.4.4) */
#define WINDOW_MS 7680

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

/* The values of the Poll Control server on ep, or NULL */
static struct zzb_zcl_poll_control *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_POLL_CONTROL, true);

	return cl != NULL ? cl->values : NULL;
}

/* Whether ep is the first endpoint with a Poll Control server */
static bool first(const struct zzb_node *node, uint8_t ep)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *e = &node->device->endpoints[i];

		if (server(e) != NULL) {
			return e->id == ep;
		}
	}

	return false;
}

static int64_t ms(uint32_t qs)
{
	return (int64_t)qs * QS_MS;
}

/* The time t of a timer event, or INT64_MAX for 0, which stands for none */
static int64_t due(int64_t t)
{
	return t != 0 ? t : INT64_MAX;
}

static bool has_clients(const struct zzb_zcl_poll_control *v)
{
	ARRAY_FOR_EACH_PTR(v->clients, c) {
		if (c->end != 0) {
			return true;
		}
	}

	return false;
}

/* The server fast polls during the window and until the deadlines of its clients */
static bool fast_polls(const struct zzb_zcl_poll_control *v)
{
	return v->window_end != 0 || has_clients(v);
}

/* The earliest check-in, end of the window or deadline of a client of v, or INT64_MAX */
static int64_t next_event(const struct zzb_zcl_poll_control *v)
{
	int64_t next = MIN(due(v->next_check_in), due(v->window_end));

	ARRAY_FOR_EACH_PTR(v->clients, c) {
		next = MIN(next, due(c->end));
	}

	return next;
}

/*
 * Arms the timer for the earliest event of the node, or stops it, and holds the fast poll rate
 * while a server fast polls
 */
static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;
	bool hold = false;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_poll_control *v = server(&node->device->endpoints[i]);

		if (v != NULL) {
			next = MIN(next, next_event(v));
			hold = hold || fast_polls(v);
		}
	}

	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_ZCL, hold);
	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.poll_control_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.poll_control_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* The next check-in follows one Check-inInterval from now (ZCL 3.16.6.3) */
static void restart(struct zzb_node *node, struct zzb_zcl_poll_control *v)
{
	v->next_check_in =
		v->check_in_interval != 0 ? k_uptime_get() + ms(v->check_in_interval) : 0;
	schedule(node);
}

/* ZCL 3.16.4.4; a check-in that cannot be sent opens no window */
static void check_in(struct zzb_node *node, const struct zzb_endpoint *ep,
		     struct zzb_zcl_poll_control *v, int64_t now)
{
	v->next_check_in = now + ms(v->check_in_interval);
	if (zzb_zcl_send_bindings(node, ep, ZZB_ZCL_POLL_CONTROL, ZZB_ZCL_FRAME_CLUSTER, true,
				  ZZB_ZCL_POLL_CONTROL_CHECK_IN, NULL, 0) == 0) {
		v->window_end = now + WINDOW_MS;
	}
}

static void tick(struct k_work *work)
{
	struct zzb_node *node = CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node,
					     zcl.poll_control_timer);
	int64_t now = k_uptime_get();

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_poll_control *v = server(ep);

		if (v == NULL) {
			continue;
		}

		if (due(v->window_end) <= now) {
			v->window_end = 0;
		}

		ARRAY_FOR_EACH_PTR(v->clients, c) {
			if (due(c->end) <= now) {
				c->end = 0;
			}
		}

		if (due(v->next_check_in) <= now) {
			check_in(node, ep, v, now);
		}
	}

	schedule(node);
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
	switch (attr) {
	case ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL:
		restart(node, values);
		break;
	case ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL:
	case ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL:
		set_intervals(node, ep, values);
		break;
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

/* ZCL 3.16.5.3: the client is bound to the server on the endpoint of the command */
static bool bound(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	return zzb_aps_bound(node, cmd->ep, ZZB_ZCL_POLL_CONTROL, cmd->src_addr, cmd->src_ep);
}

/* The entry of the source of cmd in the client table, or NULL */
static struct zzb_zcl_poll_control_client *find_client(struct zzb_zcl_poll_control *v,
						       const struct zzb_zcl_cmd *cmd)
{
	ARRAY_FOR_EACH_PTR(v->clients, c) {
		if (c->end != 0 && c->addr == cmd->src_addr && c->ep == cmd->src_ep) {
			return c;
		}
	}

	return NULL;
}

/* A free entry has end 0, so it comes before the entry that ends first */
static struct zzb_zcl_poll_control_client *new_client(struct zzb_zcl_poll_control *v,
						      const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_poll_control_client *entry = &v->clients[0];

	ARRAY_FOR_EACH_PTR(v->clients, c) {
		if (c->end < entry->end) {
			entry = c;
		}
	}

	entry->addr = cmd->src_addr;
	entry->ep = cmd->src_ep;
	return entry;
}

static void remove_client(struct zzb_zcl_poll_control *v, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_poll_control_client *c = find_client(v, cmd);

	if (c != NULL) {
		c->end = 0;
	}
}

/* ZCL 3.16.5.3: Start Fast Polling and Fast Poll Timeout, 0 for FastPollTimeout */
static int check_in_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			struct zzb_zcl_poll_control *v)
{
	struct zzb_zcl_poll_control_client *c;
	uint16_t timeout;

	if (cmd->len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (!bound(node, cmd) || v->window_end == 0) {
		return ZZB_ZCL_FAILURE;
	}

	timeout = sys_get_le16(cmd->payload + 1);
	if (above_max(v, timeout)) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	if (!cmd->payload[0]) {
		remove_client(v, cmd);
		schedule(node);
		return ZZB_ZCL_SUCCESS;
	}

	c = find_client(v, cmd);
	if (c == NULL) {
		c = new_client(v, cmd);
	}

	c->end = k_uptime_get() + ms(timeout != 0 ? timeout : v->fast_poll_timeout);
	schedule(node);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 3.16.5.4: the hold of the server ends with the deadline of its last client */
static int fast_poll_stop(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			  struct zzb_zcl_poll_control *v)
{
	bool others;

	if (!bound(node, cmd) || !fast_polls(v)) {
		return ZZB_ZCL_FAILURE;
	}

	remove_client(v, cmd);
	others = has_clients(v);
	if (!others) {
		v->window_end = 0;
	}

	schedule(node);
	return others ? ZZB_ZCL_FAILURE : ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_POLL_CONTROL_CHECK_IN_RSP:
		return check_in_rsp(node, cmd, values);
	case ZZB_ZCL_POLL_CONTROL_FAST_POLL_STOP:
		return fast_poll_stop(node, cmd, values);
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

/* The limits of the application keep their values; zzb_zcl_init() has stopped the timer */
static void init(void *values)
{
	struct zzb_zcl_poll_control *v = values;

	v->check_in_interval = CHECK_IN_INTERVAL_DEFAULT;
	v->long_poll_interval = LONG_POLL_DEFAULT;
	v->short_poll_interval = SHORT_POLL_DEFAULT;
	v->fast_poll_timeout = FAST_POLL_TIMEOUT_DEFAULT;
	v->next_check_in = 0;
	v->window_end = 0;
	memset(v->clients, 0, sizeof(v->clients));
}

/* Nothing is sent at a join; the first check-in follows one interval after the start */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	set_intervals(node, ep, values);
	restart(node, values);
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_POLL_CONTROL, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_poll_control *v = values;

	set(node, ep, ZZB_ZCL_POLL_CONTROL_CHECK_IN_INTERVAL, CHECK_IN_INTERVAL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL, LONG_POLL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL, SHORT_POLL_DEFAULT);
	set(node, ep, ZZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT, FAST_POLL_TIMEOUT_DEFAULT);
	v->window_end = 0;
	memset(v->clients, 0, sizeof(v->clients));
	schedule(node);
	set_intervals(node, ep, v);
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

void zzb_zcl_poll_control_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.poll_control_timer, tick);
	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_ZCL, false);
}
