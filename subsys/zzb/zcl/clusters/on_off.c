/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/on_off.h>

#include "common/node.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.8.1.1) */
#define REVISION 2

/* Commands received (ZCL Table 3-47) */
#define CMD_OFF    0x00
#define CMD_ON     0x01
#define CMD_TOGGLE 0x02

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_ON_OFF, ZZB_ZCL_BOOL, ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_on_off, on_off),
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL, ZZB_ZCL_BOOL, 0, struct zzb_zcl_on_off,
		     global_scene_control),
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_ON_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE, struct zzb_zcl_on_off,
		     on_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE,
		     struct zzb_zcl_on_off, off_wait_time),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint16_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_ON_OFF, attr, value);
}

/* ZCL 3.8.2.3.1 */
static void off(struct zzb_node *node, uint8_t ep)
{
	set(node, ep, ZZB_ZCL_ON_OFF_ON_OFF, false);
	set(node, ep, ZZB_ZCL_ON_OFF_ON_TIME, 0);
}

/* ZCL 3.8.2.2.2, 3.8.2.3.2 */
static void on(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_on_off *v)
{
	set(node, ep, ZZB_ZCL_ON_OFF_ON_OFF, true);
	set(node, ep, ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL, true);
	if (v->on_time == 0) {
		set(node, ep, ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 0);
	}
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	const struct zzb_zcl_on_off *v = values;

	switch (cmd->id) {
	case CMD_OFF:
		off(node, cmd->ep);
		return ZZB_ZCL_SUCCESS;
	case CMD_ON:
		on(node, cmd->ep, v);
		return ZZB_ZCL_SUCCESS;
	/* ZCL 3.8.2.3.3 */
	case CMD_TOGGLE:
		if (v->on_off) {
			off(node, cmd->ep);
		} else {
			on(node, cmd->ep, v);
		}

		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

static void init(void *values)
{
	struct zzb_zcl_on_off *v = values;

	v->on_off = false;
	v->global_scene_control = true;
	v->on_time = 0;
	v->off_wait_time = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_ON_OFF_ON_OFF, false);
	set(node, ep, ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL, true);
	set(node, ep, ZZB_ZCL_ON_OFF_ON_TIME, 0);
	set(node, ep, ZZB_ZCL_ON_OFF_OFF_WAIT_TIME, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_on_off_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
};
