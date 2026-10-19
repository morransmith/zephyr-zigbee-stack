/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/identify.h>

#include "common/node.h"
#include "common/timer.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.5.1.1) */
#define REVISION 2

/* Commands received (ZCL Table 3-32) */
#define CMD_IDENTIFY 0x00

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

/* ZCL 3.5.2.3 */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case CMD_IDENTIFY:
		if (cmd->len < 2) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		zzb_zcl_set(node, cmd->ep, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME,
			    sys_get_le16(cmd->payload));
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

void zzb_zcl_identify_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.identify_timer, tick);
}
