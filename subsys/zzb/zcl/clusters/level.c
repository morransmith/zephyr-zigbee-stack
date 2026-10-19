/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zzb/zcl/level.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.19.1.1) */
#define REVISION 2

/* The range of CurrentLevel (ZCL 3.19.2.2.1) */
#define LEVEL_MIN 1
#define LEVEL_MAX 254

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_CURRENT_LEVEL, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_level, current_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_REMAINING_TIME, ZZB_ZCL_UINT16, 0, struct zzb_zcl_level,
		     remaining_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_OPTIONS, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_level, options),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_level, on_off_transition_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_ON_LEVEL, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_level, on_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_ON_TRANSITION_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_level, on_transition_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_OFF_TRANSITION_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_level, off_transition_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_LEVEL_START_UP, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_level, start_up_current_level),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 3.10.2.5 */
static const struct zzb_zcl_scene_attr scene_attrs[] = {
	{ZZB_ZCL_LEVEL_CURRENT_LEVEL, ZZB_ZCL_UINT8},
};

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_LEVEL, attr, value);
}

/* ZCL 3.10.2.3.1 */
static int move_to_level(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	if (cmd->len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	set(node, cmd->ep, ZZB_ZCL_LEVEL_CURRENT_LEVEL,
	    CLAMP(cmd->payload[0], LEVEL_MIN, LEVEL_MAX));
	return ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_LEVEL_MOVE_TO_LEVEL:
		return move_to_level(node, cmd);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

static void init(void *values)
{
	struct zzb_zcl_level *v = values;

	*v = (struct zzb_zcl_level){
		.current_level = LEVEL_MAX,
		.on_level = 0xff,
		.on_transition_time = 0xffff,
		.off_transition_time = 0xffff,
		.start_up_current_level = v->start_up_default,
		.start_up_default = v->start_up_default,
	};
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_zcl_level *v = values;

	set(node, ep, ZZB_ZCL_LEVEL_CURRENT_LEVEL, LEVEL_MAX);
	set(node, ep, ZZB_ZCL_LEVEL_OPTIONS, 0);
	set(node, ep, ZZB_ZCL_LEVEL_ON_OFF_TRANSITION_TIME, 0);
	set(node, ep, ZZB_ZCL_LEVEL_ON_LEVEL, 0xff);
	set(node, ep, ZZB_ZCL_LEVEL_ON_TRANSITION_TIME, 0xffff);
	set(node, ep, ZZB_ZCL_LEVEL_OFF_TRANSITION_TIME, 0xffff);
	set(node, ep, ZZB_ZCL_LEVEL_START_UP, v->start_up_default);
}

const struct zzb_zcl_cluster_def zzb_zcl_level_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.scene_attrs = scene_attrs,
	.scene_attr_count = ARRAY_SIZE(scene_attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
};
