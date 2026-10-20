/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/identify.h>

#include "common/node.h"
#include "bdb/bdb.h"
#include "common/timer.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.5.1.1) */
#define REVISION 2

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_IDENTIFY_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE, struct zzb_zcl_identify,
		     identify_time),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The values of the Identify server on ep, or NULL */
static const struct zzb_zcl_identify *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_IDENTIFY, true);

	return cl != NULL ? cl->values : NULL;
}

static bool identifying(const struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_identify *v = server(&node->device->endpoints[i]);

		if (v != NULL && v->identify_time > 0) {
			return true;
		}
	}

	return false;
}

/* One tick for all servers of the node */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.identify_timer);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		const struct zzb_zcl_identify *v = server(ep);

		if (v != NULL && v->identify_time > 0) {
			zzb_zcl_set(node, ep->id, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME,
				    v->identify_time - 1);
		}
	}
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	const struct zzb_zcl_identify *v = values;

	/* The end of the identification ends finding & binding for a target (BDB 8.4 step 5) */
	if (v->identify_time == 0) {
		zzb_bdb_identify_end(node, ep);
	}

	if (identifying(node)) {
		zzb_timer_arm(&node->zcl.identify_timer, K_SECONDS(1));
	} else {
		k_work_cancel_delayable(&node->zcl.identify_timer);
	}
}

static void init(void *values)
{
	struct zzb_zcl_identify *v = values;

	v->identify_time = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME, 0);
}

/* ZCL 3.5.2.3.2.1: unicast to the requester, also for a broadcast query */
static int identify_query(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			  const struct zzb_zcl_identify *v)
{
	uint8_t rsp[2];

	if (v->identify_time == 0) {
		return ZZB_ZCL_SUCCESS;
	}

	sys_put_le16(v->identify_time, rsp);
	zzb_zcl_cmd_reply(node, cmd, ZZB_ZCL_IDENTIFY_QUERY_RSP, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.5.2.3 */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_IDENTIFY_IDENTIFY:
		if (cmd->len < 2) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		zzb_zcl_set(node, cmd->ep, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME,
			    sys_get_le16(cmd->payload));
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_IDENTIFY_QUERY:
		return identify_query(node, cmd, values);
	case ZZB_ZCL_IDENTIFY_TRIGGER_EFFECT:
		if (cmd->len < 2) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		zzb_zcl_effect(node, cmd->ep, ZZB_ZCL_IDENTIFY, cmd->payload[0], cmd->payload[1]);
		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_identify_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.changed = changed,
};

int zzb_zcl_identify(struct zzb_node *node, uint8_t ep, uint16_t time)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);

	if (e == NULL || server(e) == NULL) {
		return -ENOENT;
	}

	zzb_zcl_set(node, ep, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME, time);
	return 0;
}

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 3.5.2.4.1: the response goes to finding & binding for an initiator (BDB 8.5 step 4) */
static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (cmd->id != ZZB_ZCL_IDENTIFY_QUERY_RSP) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	zzb_bdb_identify_query_rsp(node, cmd->ep, cmd->src_addr, cmd->src_ep);
	return ZZB_ZCL_SUCCESS;
}

const struct zzb_zcl_cluster_def zzb_zcl_identify_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = client_cmd,
};

void zzb_zcl_identify_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.identify_timer, tick);
}
