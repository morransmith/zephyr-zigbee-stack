/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/ias_wd.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 8.4.1.1) */
#define REVISION 2

/* MaxDuration (ZCL Table 8-19) */
#define MAX_DURATION_DEFAULT 240
#define MAX_DURATION_INVALID 0xffff

/* The octet of the fields, warning duration, strobe duty cycle and strobe level */
#define START_WARNING_LEN 5

#define WARNING_MODE_MAX 6
#define STROBE_LEVEL_MAX 3
#define DUTY_CYCLE_MAX   100

/* Stop is warning mode 0 without strobe (ZCL 8.4.2.2.1.3) */
#define WARNING_FIELDS (ZZB_ZCL_IAS_WD_WARNING_MODE | ZZB_ZCL_IAS_WD_STROBE)

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_WD_MAX_DURATION, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ias_wd, max_duration),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The values of the IAS WD server on ep, or NULL */
static struct zzb_zcl_ias_wd *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_IAS_WD, true);

	return cl != NULL ? cl->values : NULL;
}

/* Arms the timer for the earliest end of a warning of the node, or stops it */
static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_ias_wd *v = server(&node->device->endpoints[i]);

		if (v != NULL && v->warning_end != 0) {
			next = MIN(next, v->warning_end);
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.ias_wd_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.ias_wd_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* Ends the warning on ep and passes Start Warning with all fields 0 to zcl_cmd_cb */
static void end_warning(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ias_wd *v)
{
	static const uint8_t stop[START_WARNING_LEN];
	const struct zzb_zcl_cmd cmd = {
		.ep = ep,
		.cluster = ZZB_ZCL_IAS_WD,
		.id = ZZB_ZCL_IAS_WD_START_WARNING,
		.payload = stop,
		.len = sizeof(stop),
		.server = true,
	};

	v->warning_end = 0;
	zzb_zcl_app_cmd(node, &cmd);
}

static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.ias_wd_timer);
	int64_t now = k_uptime_get();

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_ias_wd *v = server(ep);

		if (v != NULL && v->warning_end != 0 && v->warning_end <= now) {
			end_warning(node, ep->id, v);
		}
	}

	schedule(node);
}

/* ZCL 8.4.2.2.1.6: the nearest multiple of 10, 5 upwards */
static uint8_t duty_cycle(uint8_t value)
{
	return MIN((value + 5) / 10 * 10, DUTY_CYCLE_MAX);
}

/*
 * ZCL 8.4.2.2.1. MaxDuration counts from the start of the continuous warning (ZCL 8.4.2.1.1), and
 * the time left is rounded down, so that the warning does not last longer.
 */
static int start_warning(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_ias_wd *v)
{
	/* A copy of the whole request, which zzb_zcl_cmd_respond() can answer */
	struct zzb_zcl_req req = *CONTAINER_OF(cmd, struct zzb_zcl_req, cmd);
	int64_t now = k_uptime_get();
	int64_t start = v->warning_end != 0 ? v->warning_start : now;
	int64_t left = MAX(start + v->max_duration * MSEC_PER_SEC - now, 0) / MSEC_PER_SEC;
	uint8_t payload[START_WARNING_LEN];
	uint16_t duration;
	int status;

	if (cmd->len < START_WARNING_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (FIELD_GET(ZZB_ZCL_IAS_WD_WARNING_MODE, cmd->payload[0]) > WARNING_MODE_MAX ||
	    FIELD_GET(ZZB_ZCL_IAS_WD_STROBE, cmd->payload[0]) > 1 ||
	    cmd->payload[4] > STROBE_LEVEL_MAX) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	memcpy(payload, cmd->payload, sizeof(payload));
	duration = MIN(sys_get_le16(&payload[1]), left);
	sys_put_le16(duration, &payload[1]);
	payload[3] = duty_cycle(payload[3]);
	req.cmd.payload = payload;
	req.cmd.len = sizeof(payload);
	status = zzb_zcl_app_cmd(node, &req.cmd);
	if (status != ZZB_ZCL_SUCCESS) {
		return status;
	}

	if (!(payload[0] & WARNING_FIELDS)) {
		v->warning_end = 0;
	} else if (duration > 0) {
		v->warning_start = start;
		v->warning_end = now + duration * MSEC_PER_SEC;
	}

	schedule(node);
	return status;
}

/* ZCL 8.4.2.2.2; Squawk has no effect during a warning */
static int squawk(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		  const struct zzb_zcl_ias_wd *v)
{
	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (FIELD_GET(ZZB_ZCL_IAS_WD_SQUAWK_MODE, cmd->payload[0]) > 1) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	if (v->warning_end != 0) {
		return ZZB_ZCL_SUCCESS;
	}

	return zzb_zcl_app_cmd(node, cmd);
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_IAS_WD_START_WARNING:
		return start_warning(node, cmd, values);
	case ZZB_ZCL_IAS_WD_SQUAWK:
		return squawk(node, cmd, values);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* MaxDuration is the only writable attribute */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	return value == MAX_DURATION_INVALID ? ZZB_ZCL_INVALID_VALUE : ZZB_ZCL_SUCCESS;
}

/* zzb_zcl_init() has stopped the timer, so no warning runs */
static void init(void *values)
{
	struct zzb_zcl_ias_wd *v = values;

	v->max_duration = MAX_DURATION_DEFAULT;
	v->warning_end = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_ias_wd *v = values;

	zzb_zcl_set(node, ep, ZZB_ZCL_IAS_WD, ZZB_ZCL_IAS_WD_MAX_DURATION, MAX_DURATION_DEFAULT);
	if (v->warning_end != 0) {
		end_warning(node, ep, v);
		schedule(node);
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_ias_wd_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
};

void zzb_zcl_ias_wd_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.ias_wd_timer, tick);
}

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_ias_wd_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
