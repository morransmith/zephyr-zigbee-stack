/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/level.h>

#include "common/node.h"
#include "common/timer.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.19.1.1) */
#define REVISION 2

/* The range of CurrentLevel (ZCL 3.19.2.2.1) */
#define LEVEL_MIN 1
#define LEVEL_MAX 254

/* Period of the steps of a transition, the unit of ZCL transition times */
#define STEP_MS 100

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

/* The Level Control server on ep, or NULL */
static const struct zzb_zcl_cluster *server(const struct zzb_endpoint *ep)
{
	return zzb_zcl_find_cluster(ep, ZZB_ZCL_LEVEL, true);
}

/* The values of the server cl while it runs a transition, or NULL */
static struct zzb_zcl_level *moving(const struct zzb_zcl_cluster *cl)
{
	struct zzb_zcl_level *v = cl != NULL ? cl->values : NULL;

	return v != NULL && v->transition.count > 0 ? v : NULL;
}

/* Arms the timer while a transition runs */
static void schedule(struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		if (moving(server(&node->device->endpoints[i])) != NULL) {
			zzb_timer_arm(&node->zcl.level_timer, K_MSEC(STEP_MS));
			return;
		}
	}

	k_work_cancel_delayable(&node->zcl.level_timer);
}

/* RemainingTime follows the transition (ZCL 3.10.2.2.2) */
static void remaining(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_level *v)
{
	zzb_zcl_update(node, ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_REMAINING_TIME,
		       zzb_zcl_transition_remaining(&v->transition));
}

/* One step of the transitions of all servers of the node */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.level_timer);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		const struct zzb_zcl_cluster *cl = server(ep);
		struct zzb_zcl_level *v = moving(cl);

		if (v == NULL) {
			continue;
		}

		zzb_zcl_transition_step(node, ep->id, cl, &v->transition);
		remaining(node, ep->id, v);
	}

	schedule(node);
}

/*
 * Moves CurrentLevel to level over time_ds tenths of a second, at once for 0, and replaces a
 * running transition. A transition sets SceneValid FALSE at its start, since its steps do not.
 */
static void move(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v, uint8_t level,
		 uint32_t time_ds)
{
	const uint16_t attr = ZZB_ZCL_LEVEL_CURRENT_LEVEL;
	const uint16_t to = level;

	v->transition.count = 0;
	if (time_ds == 0) {
		set(node, ep, ZZB_ZCL_LEVEL_CURRENT_LEVEL, level);
	} else {
		if (level != v->current_level) {
			zzb_zcl_scenes_invalidate(node, ep, true);
		}

		zzb_zcl_transition_start(&v->transition, server(zzb_node_endpoint(node, ep)), &attr,
					 &to, 1, time_ds);
	}

	remaining(node, ep, v);
	schedule(node);
}

/* ZCL 3.10.2.3.1 */
static int move_to_level(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_level *v)
{
	uint16_t time;

	if (cmd->len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	time = sys_get_le16(cmd->payload + 1);
	move(node, cmd->ep, v, CLAMP(cmd->payload[0], LEVEL_MIN, LEVEL_MAX),
	     time == 0xffff ? v->on_off_transition_time : time);
	return ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_LEVEL_MOVE_TO_LEVEL:
		return move_to_level(node, cmd, values);
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
	struct zzb_zcl_level *v = values;

	v->transition.count = 0;
	remaining(node, ep, v);
	schedule(node);
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

void zzb_zcl_level_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.level_timer, tick);
}
