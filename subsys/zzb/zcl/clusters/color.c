/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/color.h>

#include "common/node.h"
#include "common/timer.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 5.2.1.1) */
#define REVISION 3

/* The highest CurrentX and CurrentY (ZCL 5.2.2.2.1.4) */
#define XY_MAX 0xfeff

/* The defaults of CurrentX, CurrentY and ColorTemperatureMireds (ZCL Table 5.3) */
#define DEFAULT_X           0x616b
#define DEFAULT_Y           0x607d
#define DEFAULT_TEMPERATURE 0x00fa

/* Period of the steps of a transition, the unit of ZCL transition times */
#define STEP_MS 100

/* The WhitePoint and ColorPoint attributes are not implemented */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_REMAINING_TIME, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     remaining_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_color, current_x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_CURRENT_Y, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_color, current_y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_TEMPERATURE, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_color, color_temperature),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_NV, struct zzb_zcl_color,
		     color_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_OPTIONS, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_color, options),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_NUMBER_OF_PRIMARIES, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     number_of_primaries),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY1_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[0].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY1_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[0].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY1_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[0].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY2_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[1].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY2_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[1].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY2_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[1].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY3_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[2].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY3_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[2].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY3_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[2].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY4_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[3].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY4_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[3].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY4_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[3].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY5_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[4].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY5_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[4].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY5_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[4].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY6_X, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[5].x),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY6_Y, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     primaries[5].y),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_PRIMARY6_INTENSITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_color,
		     primaries[5].intensity),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_ENHANCED_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_NV, struct zzb_zcl_color,
		     enhanced_color_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_CAPABILITIES, ZZB_ZCL_MAP16, 0, struct zzb_zcl_color,
		     color_capabilities),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_TEMP_PHYSICAL_MIN, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     color_temp_physical_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_TEMP_PHYSICAL_MAX, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     color_temp_physical_max),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_COUPLE_TEMP_MIN, ZZB_ZCL_UINT16, 0, struct zzb_zcl_color,
		     couple_color_temp_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_COLOR_START_UP_TEMPERATURE, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_color, start_up_color_temperature),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_COLOR, attr, value);
}

/* The Color Control server on ep, or NULL */
static const struct zzb_zcl_cluster *server(const struct zzb_endpoint *ep)
{
	return zzb_zcl_find_cluster(ep, ZZB_ZCL_COLOR, true);
}

/* The values of the server cl while it runs a transition, or NULL */
static struct zzb_zcl_color *moving(const struct zzb_zcl_cluster *cl)
{
	struct zzb_zcl_color *v = cl != NULL ? cl->values : NULL;

	return v != NULL && v->transition.count > 0 ? v : NULL;
}

/* Arms the timer while a transition runs */
static void schedule(struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		if (moving(server(&node->device->endpoints[i])) != NULL) {
			zzb_timer_arm(&node->zcl.color_timer, K_MSEC(STEP_MS));
			return;
		}
	}

	k_work_cancel_delayable(&node->zcl.color_timer);
}

/* RemainingTime follows the transition (ZCL 5.2.2.2.1.3) */
static void remaining(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_color *v)
{
	zzb_zcl_update(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_REMAINING_TIME,
		       zzb_zcl_transition_remaining(&v->transition));
}

/* One step of the transitions of all servers of the node */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.color_timer);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		const struct zzb_zcl_cluster *cl = server(ep);
		struct zzb_zcl_color *v = moving(cl);

		if (v == NULL) {
			continue;
		}

		zzb_zcl_transition_step(node, ep->id, cl, &v->transition);
		remaining(node, ep->id, v);
	}

	schedule(node);
}

/*
 * Moves count attributes attrs to the values to over time_ds tenths of a second, at once for 0,
 * and replaces a running transition
 */
static void move(struct zzb_node *node, uint8_t ep, struct zzb_zcl_color *v, const uint16_t *attrs,
		 const uint16_t *to, size_t count, uint32_t time_ds)
{
	v->transition.count = 0;
	if (time_ds > 0) {
		zzb_zcl_transition_start(&v->transition, server(zzb_node_endpoint(node, ep)), attrs,
					 to, count, time_ds);
	} else {
		for (size_t i = 0; i < count; i++) {
			set(node, ep, attrs[i], to[i]);
		}
	}

	remaining(node, ep, v);
	schedule(node);
}

/* X and Y move together, on a straight line in the xy space (ZCL 5.2.2.3.11) */
static void move_xy(struct zzb_node *node, uint8_t ep, struct zzb_zcl_color *v, uint16_t x,
		    uint16_t y, uint32_t time_ds)
{
	const uint16_t attrs[] = {ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_COLOR_CURRENT_Y};
	const uint16_t to[] = {MIN(x, XY_MAX), MIN(y, XY_MAX)};

	move(node, ep, v, attrs, to, ARRAY_SIZE(attrs), time_ds);
}

/* The attributes stay and are saved, and RemainingTime becomes 0 (ZCL 5.2.2.3.20) */
static void stop(struct zzb_node *node, uint8_t ep, struct zzb_zcl_color *v)
{
	zzb_zcl_transition_stop(node, ep, ZZB_ZCL_COLOR, &v->transition);
	remaining(node, ep, v);
	schedule(node);
}

/* ColorMode and EnhancedColorMode take mode, the first action of a command (ZCL 5.2.2.3.2) */
static void set_mode(struct zzb_node *node, uint8_t ep, uint8_t mode)
{
	set(node, ep, ZZB_ZCL_COLOR_MODE, mode);
	set(node, ep, ZZB_ZCL_COLOR_ENHANCED_MODE, mode);
}

/* ZCL 5.2.2.3.11 */
static int move_to_color(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_color *v)
{
	set_mode(node, cmd->ep, ZZB_ZCL_COLOR_MODE_XY);
	move_xy(node, cmd->ep, v, sys_get_le16(cmd->payload), sys_get_le16(cmd->payload + 2),
		sys_get_le16(cmd->payload + 4));
	return ZZB_ZCL_SUCCESS;
}

/* The distance of value to the limit of X and Y in the direction of rate */
static uint32_t to_limit(uint16_t value, int16_t rate)
{
	return rate > 0 ? XY_MAX - value : value;
}

/*
 * ZCL 5.2.2.3.12: the rates are in steps per second. The move ends when X or Y reaches its limit,
 * so that both stop together on a straight line. Both rates 0 stop the transition.
 */
static int move_color(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_color *v)
{
	int16_t rate_x = sys_get_le16(cmd->payload);
	int16_t rate_y = sys_get_le16(cmd->payload + 2);
	uint32_t dx = to_limit(v->current_x, rate_x);
	uint32_t dy = to_limit(v->current_y, rate_y);
	/* X ends the move when it needs less time: dx / |rate_x| <= dy / |rate_y| */
	bool x_ends = rate_y == 0 || (rate_x != 0 && dx * abs(rate_y) <= dy * abs(rate_x));
	int32_t distance = x_ends ? dx : dy;
	int rate = abs(x_ends ? rate_x : rate_y);

	if (rate_x == 0 && rate_y == 0) {
		stop(node, cmd->ep, v);
		return ZZB_ZCL_SUCCESS;
	}

	set_mode(node, cmd->ep, ZZB_ZCL_COLOR_MODE_XY);
	move_xy(node, cmd->ep, v, v->current_x + rate_x * distance / rate,
		v->current_y + rate_y * distance / rate, 10 * distance / rate);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 5.2.2.3.13 */
static int step_color(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_color *v)
{
	int16_t step_x = sys_get_le16(cmd->payload);
	int16_t step_y = sys_get_le16(cmd->payload + 2);

	set_mode(node, cmd->ep, ZZB_ZCL_COLOR_MODE_XY);
	move_xy(node, cmd->ep, v, CLAMP(v->current_x + step_x, 0, XY_MAX),
		CLAMP(v->current_y + step_y, 0, XY_MAX), sys_get_le16(cmd->payload + 4));
	return ZZB_ZCL_SUCCESS;
}

/* The commands with the length of their fields before OptionsMask and their capability */
static const struct {
	uint8_t id;
	uint8_t len;
	uint16_t capability;
	int (*fn)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_color *v);
} commands[] = {
	{ZZB_ZCL_COLOR_MOVE_TO_COLOR, 6, ZZB_ZCL_COLOR_CAP_XY, move_to_color},
	{ZZB_ZCL_COLOR_MOVE_COLOR, 4, ZZB_ZCL_COLOR_CAP_XY, move_color},
	{ZZB_ZCL_COLOR_STEP_COLOR, 6, ZZB_ZCL_COLOR_CAP_XY, step_color},
};

/* A command of a capability that the light lacks is unsupported (ZCL 5.2.2.2.1.18) */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	struct zzb_zcl_color *v = values;

	ARRAY_FOR_EACH(commands, i) {
		if (commands[i].id != cmd->id ||
		    !(v->color_capabilities & commands[i].capability)) {
			continue;
		}

		if (cmd->len < commands[i].len) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		return commands[i].fn(node, cmd, v);
	}

	return ZZB_ZCL_UNSUP_COMMAND;
}

/* A light with color temperature starts in its mode */
static uint8_t default_mode(const struct zzb_zcl_color *v)
{
	return v->color_capabilities & ZZB_ZCL_COLOR_CAP_TEMPERATURE
		       ? ZZB_ZCL_COLOR_MODE_TEMPERATURE
		       : ZZB_ZCL_COLOR_MODE_XY;
}

static uint16_t default_temperature(const struct zzb_zcl_color *v)
{
	return CLAMP(DEFAULT_TEMPERATURE, v->color_temp_physical_min, v->color_temp_physical_max);
}

static void init(void *values)
{
	struct zzb_zcl_color *v = values;

	v->remaining_time = 0;
	v->current_x = DEFAULT_X;
	v->current_y = DEFAULT_Y;
	v->color_temperature = default_temperature(v);
	v->color_mode = default_mode(v);
	v->enhanced_color_mode = v->color_mode;
	v->options = 0;
	v->start_up_color_temperature = v->start_up_default;
	v->transition.count = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_color *v = values;

	v->transition.count = 0;
	remaining(node, ep, v);
	schedule(node);
	set_mode(node, ep, default_mode(v));
	set(node, ep, ZZB_ZCL_COLOR_CURRENT_X, DEFAULT_X);
	set(node, ep, ZZB_ZCL_COLOR_CURRENT_Y, DEFAULT_Y);
	set(node, ep, ZZB_ZCL_COLOR_TEMPERATURE, default_temperature(v));
	set(node, ep, ZZB_ZCL_COLOR_OPTIONS, 0);
	set(node, ep, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, v->start_up_default);
}

const struct zzb_zcl_cluster_def zzb_zcl_color_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
};

void zzb_zcl_color_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.color_timer, tick);
}
