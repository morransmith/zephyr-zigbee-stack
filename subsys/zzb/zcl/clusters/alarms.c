/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/alarms.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.11.1.1) */
#define REVISION 1

/* Time stamp of an alarm without a Time server (ZCL Table 3-66) */
#define NO_TIME 0xffffffff

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ALARMS_ALARM_COUNT, ZZB_ZCL_UINT16, 0, struct zzb_zcl_alarms,
		     alarm_count),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static bool has_server(struct zzb_node *node, uint8_t ep)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);

	return e != NULL && zzb_zcl_find_cluster(e, ZZB_ZCL_ALARMS, true) != NULL;
}

/* AlarmCount follows every change of the table */
static void count(struct zzb_node *node, uint8_t ep, bool notify)
{
	int n = 0;

	ARRAY_FOR_EACH_PTR(node->zcl.alarms, a) {
		n += a->in_use && a->ep == ep;
	}

	if (notify) {
		zzb_zcl_set(node, ep, ZZB_ZCL_ALARMS, ZZB_ZCL_ALARMS_ALARM_COUNT, n);
	} else {
		zzb_zcl_store(node, ep, ZZB_ZCL_ALARMS, ZZB_ZCL_ALARMS_ALARM_COUNT, n);
	}
}

/* The earliest entry of endpoint ep, or NULL */
static struct zzb_zcl_alarm *earliest(struct zzb_node *node, uint8_t ep)
{
	struct zzb_zcl_alarm *first = NULL;

	ARRAY_FOR_EACH_PTR(node->zcl.alarms, a) {
		if (a->in_use && a->ep == ep && (first == NULL || a->seq < first->seq)) {
			first = a;
		}
	}

	return first;
}

/* A free entry, or the earliest one, which a full table replaces (ZCL 3.11.2.3) */
static struct zzb_zcl_alarm *new_entry(struct zzb_node *node)
{
	struct zzb_zcl_alarm *entry = &node->zcl.alarms[0];

	ARRAY_FOR_EACH_PTR(node->zcl.alarms, a) {
		if (!a->in_use) {
			return a;
		}

		if (a->seq < entry->seq) {
			entry = a;
		}
	}

	return entry;
}

static uint32_t next_seq(struct zzb_node *node)
{
	uint32_t seq = 0;

	ARRAY_FOR_EACH_PTR(node->zcl.alarms, a) {
		if (a->in_use) {
			seq = MAX(seq, a->seq);
		}
	}

	return seq + 1;
}

void zzb_zcl_alarm_raise(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t code,
			 bool send, bool notify)
{
	struct zzb_zcl_alarm *entry;
	uint8_t replaced;
	uint32_t seq;

	if (!has_server(node, ep)) {
		return;
	}

	seq = next_seq(node);
	entry = new_entry(node);
	replaced = entry->in_use ? entry->ep : ep;
	*entry = (struct zzb_zcl_alarm){
		.in_use = true,
		.seq = seq,
		.ep = ep,
		.code = code,
		.cluster = cluster,
	};
	count(node, replaced, notify);
	count(node, ep, notify);
}

/* ZCL 3.11.2.4.3; the response also answers a broadcast */
static int get_alarm(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_alarm *a = earliest(node, cmd->ep);
	uint8_t rsp[8] = {ZZB_ZCL_NOT_FOUND};

	if (a == NULL) {
		zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_ALARMS_GET_ALARM_RSP, rsp, 1);
		return ZZB_ZCL_RESPONDED;
	}

	rsp[0] = ZZB_ZCL_SUCCESS;
	rsp[1] = a->code;
	sys_put_le16(a->cluster, rsp + 2);
	sys_put_le32(NO_TIME, rsp + 4);
	a->in_use = false;
	count(node, cmd->ep, true);
	zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_ALARMS_GET_ALARM_RSP, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.11.2.4.4: the entries of the endpoint */
static int reset_log(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	ARRAY_FOR_EACH_PTR(node->zcl.alarms, a) {
		if (a->in_use && a->ep == cmd->ep) {
			a->in_use = false;
		}
	}

	count(node, cmd->ep, true);
	return ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_ALARMS_GET_ALARM:
		return get_alarm(node, cmd);
	case ZZB_ZCL_ALARMS_RESET_ALARM_LOG:
		return reset_log(node, cmd);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

static void init(void *values)
{
	*(struct zzb_zcl_alarms *)values = (struct zzb_zcl_alarms){0};
}

const struct zzb_zcl_cluster_def zzb_zcl_alarms_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.cmd = cmd,
};

void zzb_zcl_alarms_reset(struct zzb_node *node)
{
	memset(node->zcl.alarms, 0, sizeof(node->zcl.alarms));
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		count(node, node->device->endpoints[i].id, true);
	}
}

struct alarm_req {
	struct k_work work;
	struct zzb_node *node;
	uint8_t ep;
	uint16_t cluster;
	uint8_t code;
	int err;
};

static void alarm_handler(struct k_work *work)
{
	struct alarm_req *req = CONTAINER_OF(work, struct alarm_req, work);

	if (!has_server(req->node, req->ep)) {
		req->err = -ENOENT;
		return;
	}

	zzb_zcl_alarm_raise(req->node, req->ep, req->cluster, req->code, true, false);
}

int zzb_zcl_alarm(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t code)
{
	struct alarm_req req = {
		.node = node,
		.ep = ep,
		.cluster = cluster,
		.code = code,
	};

	zzb_call_sync(&req.work, alarm_handler);
	return req.err;
}
