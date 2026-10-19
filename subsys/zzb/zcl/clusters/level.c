/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/on_off.h>

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

/* Move mode and step mode (ZCL Table 3-60, 3-62) */
#define MODE_UP   0x00
#define MODE_DOWN 0x01

/* Bits of Options (ZCL Table 3-57) */
#define EXECUTE_IF_OFF BIT(0)

/* Commands 0x04 to 0x07 are 0x00 to 0x03 with On/Off (ZCL Table 3-59) */
#define WITH_ON_OFF 0x04

/* On and Toggle of the On/Off cluster (ZCL Table 3-47); Off is the other command */
#define ON_OFF_ON     0x01
#define ON_OFF_TOGGLE 0x02

/* The change of OnOff at the end of a transition: none, OnOff FALSE, the end of the fade of Off */
#define END_NONE 0
#define END_OFF  1
#define END_FADE 2

/* The fields of Move to Level, Move, Step and Stop before OptionsMask, in octets (ZCL 3.10.2.3) */
static const uint8_t fields_len[] = {3, 2, 4, 0};

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

/* The values of the On/Off server on ep, or NULL */
static const struct zzb_zcl_on_off *on_off_server(struct zzb_node *node, uint8_t ep)
{
	const struct zzb_zcl_cluster *cl =
		zzb_zcl_find_cluster(zzb_node_endpoint(node, ep), ZZB_ZCL_ON_OFF, true);

	return cl != NULL ? cl->values : NULL;
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

/*
 * Applies the change of OnOff at the end of a transition. The fade of Off restores the stored
 * level with OnLevel 0xff (ZCL Table 3-55).
 */
static void finish(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v)
{
	uint8_t end = v->end;

	v->end = END_NONE;
	if (end == END_NONE) {
		return;
	}

	zzb_zcl_on_off_switch(node, ep, false);
	if (end == END_FADE && v->on_level == 0xff) {
		set(node, ep, ZZB_ZCL_LEVEL_CURRENT_LEVEL, v->stored_level);
	}
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
		bool ended;

		if (v == NULL) {
			continue;
		}

		ended = zzb_zcl_transition_step(node, ep->id, cl, &v->transition);
		remaining(node, ep->id, v);
		if (ended) {
			finish(node, ep->id, v);
		}
	}

	schedule(node);
}

/*
 * Moves CurrentLevel to level over time_ds tenths of a second, at once for 0, and replaces a
 * running transition; end is the change of OnOff at its end. A transition sets SceneValid FALSE
 * at its start, since its steps do not.
 */
static void move(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v, uint8_t level,
		 uint32_t time_ds, uint8_t end)
{
	const uint16_t attr = ZZB_ZCL_LEVEL_CURRENT_LEVEL;
	const uint16_t to = level;

	v->transition.count = 0;
	v->end = end;
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
	if (time_ds == 0) {
		finish(node, ep, v);
	}
}

/* With On/Off the endpoint turns on before a move above 1 and off after a move to 1 */
static void command_move(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_level *v, uint8_t level, uint32_t time_ds)
{
	bool on_off = cmd->id & WITH_ON_OFF;

	if (on_off && level > LEVEL_MIN) {
		zzb_zcl_on_off_switch(node, cmd->ep, true);
	}

	move(node, cmd->ep, v, level, time_ds, on_off && level == LEVEL_MIN ? END_OFF : END_NONE);
}

/* ZCL 3.10.2.3.1 */
static int move_to_level(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_level *v)
{
	uint16_t time = sys_get_le16(cmd->payload + 1);

	command_move(node, cmd, v, CLAMP(cmd->payload[0], LEVEL_MIN, LEVEL_MAX),
		     time == 0xffff ? v->on_off_transition_time : time);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 3.10.2.3.2: rate 0xff moves at once, since DefaultMoveRate is not implemented */
static int move_at_rate(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			struct zzb_zcl_level *v)
{
	uint8_t mode = cmd->payload[0];
	uint8_t rate = cmd->payload[1];
	uint8_t level = mode == MODE_UP ? LEVEL_MAX : LEVEL_MIN;

	if (mode > MODE_DOWN || rate == 0) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	/* The rate is in levels per second */
	command_move(node, cmd, v, level,
		     rate == 0xff ? 0 : 10 * abs(level - v->current_level) / rate);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 3.10.2.3.3: transition time 0xffff steps at once */
static int step(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_level *v)
{
	uint8_t mode = cmd->payload[0];
	uint8_t size = cmd->payload[1];
	uint16_t time = sys_get_le16(cmd->payload + 2);
	uint8_t level =
		CLAMP(v->current_level + (mode == MODE_UP ? size : -size), LEVEL_MIN, LEVEL_MAX);

	if (mode > MODE_DOWN) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	/* A step cut at a limit takes the same part of the transition time */
	command_move(node, cmd, v, level,
		     time == 0xffff || size == 0 ? 0 : time * abs(level - v->current_level) / size);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 3.10.2.3.4: the level stays and is saved, and OnOff does not change at the end */
static void stop(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v)
{
	v->end = END_NONE;
	zzb_zcl_transition_stop(node, ep, ZZB_ZCL_LEVEL, &v->transition);
	remaining(node, ep, v);
	schedule(node);
}

/*
 * ZCL 3.10.2.2.8.1: while the On/Off server on the endpoint is off, a command without On/Off runs
 * only with ExecuteIfOff in its options, in which OptionsMask selects the bits of OptionsOverride
 * (ZCL 3.10.2.3.1.2)
 */
static bool executes(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		     const struct zzb_zcl_level *v)
{
	const struct zzb_zcl_on_off *on_off = on_off_server(node, cmd->ep);
	const uint8_t *p = cmd->payload + fields_len[cmd->id];
	uint8_t options = v->options;

	if (cmd->len >= fields_len[cmd->id] + 2) {
		options = (options & ~p[0]) | (p[1] & p[0]);
	}

	return on_off == NULL || on_off->on_off || (options & EXECUTE_IF_OFF);
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	uint8_t id = cmd->id & ~WITH_ON_OFF;

	if (cmd->id > ZZB_ZCL_LEVEL_STOP_ON_OFF) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->len < fields_len[id]) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (!(cmd->id & WITH_ON_OFF) && !executes(node, cmd, values)) {
		return ZZB_ZCL_SUCCESS;
	}

	switch (id) {
	case ZZB_ZCL_LEVEL_MOVE_TO_LEVEL:
		return move_to_level(node, cmd, values);
	case ZZB_ZCL_LEVEL_MOVE:
		return move_at_rate(node, cmd, values);
	case ZZB_ZCL_LEVEL_STEP:
		return step(node, cmd, values);
	default:
		stop(node, cmd->ep, values);
		return ZZB_ZCL_SUCCESS;
	}
}

/*
 * The time of a fade over distance levels: OnTransitionTime or OffTransitionTime as time, which ZCL
 * gives for the whole range of levels (ZCL 3.10.2.2.11), or OnOffTransitionTime for 0xffff
 */
static uint32_t fade_time(const struct zzb_zcl_level *v, uint16_t time, int distance)
{
	if (time == 0xffff) {
		return v->on_off_transition_time;
	}

	return time * distance / (LEVEL_MAX - LEVEL_MIN);
}

/*
 * On while off: the level is stored and moves from 1 to OnLevel, or with OnLevel 0xff to the
 * stored level. During the fade of Off it moves back to the stored level (ZCL 3.10.2.1.1).
 */
static void fade_up(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v, bool fading)
{
	uint8_t from = fading ? v->current_level : LEVEL_MIN;
	uint8_t level;
	uint32_t time;

	if (!fading) {
		v->stored_level = v->current_level;
	}

	level = fading || v->on_level == 0xff ? v->stored_level
					      : CLAMP(v->on_level, LEVEL_MIN, LEVEL_MAX);
	time = fade_time(v, v->on_transition_time, abs(level - from));
	if (time > 0 && !fading) {
		set(node, ep, ZZB_ZCL_LEVEL_CURRENT_LEVEL, LEVEL_MIN);
	}

	zzb_zcl_on_off_switch(node, ep, true);
	move(node, ep, v, level, time, END_NONE);
}

/*
 * Off while on: the level is stored and moves to 1, and OnOff becomes FALSE at the end. OnTime
 * becomes 0 at once, so that its timer does not turn the light off during the fade.
 */
static void fade_down(struct zzb_node *node, uint8_t ep, struct zzb_zcl_level *v)
{
	uint32_t time = fade_time(v, v->off_transition_time, abs(v->current_level - LEVEL_MIN));

	v->stored_level = v->current_level;
	zzb_zcl_set(node, ep, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_TIME, 0);

	/* At once, with OnLevel 0xff the level would return to itself */
	move(node, ep, v, time == 0 && v->on_level == 0xff ? v->stored_level : LEVEL_MIN, time,
	     END_FADE);
}

int zzb_zcl_level_on_off(struct zzb_node *node, uint8_t ep, uint8_t cmd)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);
	const struct zzb_zcl_cluster *cl = e != NULL ? server(e) : NULL;
	struct zzb_zcl_level *v;
	bool on, fading, up;

	if (cl == NULL) {
		return -ENOENT;
	}

	v = cl->values;
	on = on_off_server(node, ep)->on_off;
	fading = moving(cl) != NULL && v->end == END_FADE;
	up = cmd == ON_OFF_ON || (cmd == ON_OFF_TOGGLE && (!on || fading));

	/* On while on and Off while off leave the level */
	if (up == on && !fading) {
		return -EALREADY;
	}

	/* Off during the fade of Off is ignored */
	if (up) {
		fade_up(node, ep, v, fading);
	} else if (!fading) {
		fade_down(node, ep, v);
	}

	return 0;
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

/* ZCL Table 3-58: 0x00 sets the minimum, 0xff keeps the loaded level */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_zcl_level *v = values;

	if (v->start_up_current_level == 0xff) {
		return;
	}

	zzb_zcl_store(node, ep, ZZB_ZCL_LEVEL, ZZB_ZCL_LEVEL_CURRENT_LEVEL,
		      CLAMP(v->start_up_current_level, LEVEL_MIN, LEVEL_MAX));
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_level *v = values;

	v->transition.count = 0;
	v->end = END_NONE;
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
	.start = start,
	.reset = reset,
	.cmd = cmd,
};

void zzb_zcl_level_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.level_timer, tick);
}
