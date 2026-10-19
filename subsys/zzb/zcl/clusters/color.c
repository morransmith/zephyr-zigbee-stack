/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/on_off.h>

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

/* Move mode and step mode of the color temperature commands (ZCL Tables 5.15, 5.17) */
#define MODE_STOP 0x00
#define MODE_UP   0x01
#define MODE_DOWN 0x03

/* Bits of Options (ZCL Table 5.6) */
#define EXECUTE_IF_OFF BIT(0)

/* Attributes of the scene extension field set that the server does not have (ZCL Table 5.3) */
#define CURRENT_SATURATION   0x0001
#define ENHANCED_CURRENT_HUE 0x4000
#define COLOR_LOOP_ACTIVE    0x4002
#define COLOR_LOOP_DIRECTION 0x4003
#define COLOR_LOOP_TIME      0x4004

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

/* ZCL 5.2.2.5, in this order; the attributes that the server does not have are stored as 0 */
static const struct zzb_zcl_scene_attr scene_attrs[] = {
	{ZZB_ZCL_COLOR_CURRENT_X, ZZB_ZCL_UINT16}, {ZZB_ZCL_COLOR_CURRENT_Y, ZZB_ZCL_UINT16},
	{ENHANCED_CURRENT_HUE, ZZB_ZCL_UINT16},    {CURRENT_SATURATION, ZZB_ZCL_UINT8},
	{COLOR_LOOP_ACTIVE, ZZB_ZCL_UINT8},        {COLOR_LOOP_DIRECTION, ZZB_ZCL_UINT8},
	{COLOR_LOOP_TIME, ZZB_ZCL_UINT16},         {ZZB_ZCL_COLOR_TEMPERATURE, ZZB_ZCL_UINT16},
};

/*
 * CurrentX and CurrentY of the point at mireds on the Planckian locus, by the cubic spline of Kim
 * et al. (2002) for 1667 K to 25000 K
 */
static void temperature_xy(uint16_t mireds, uint16_t *cx, uint16_t *cy)
{
	float t = CLAMP(1e6f / MAX(mireds, 1), 1667.0f, 25000.0f);
	float u = 1.0f / t;
	float x, y;

	if (t <= 4000.0f) {
		x = ((-0.2661239e9f * u - 0.2343589e6f) * u + 0.8776956e3f) * u + 0.179910f;
	} else {
		x = ((-3.0258469e9f * u + 2.1070379e6f) * u + 0.2226347e3f) * u + 0.240390f;
	}

	if (t <= 2222.0f) {
		y = ((-1.1063814f * x - 1.34811020f) * x + 2.18555832f) * x - 0.20219683f;
	} else if (t <= 4000.0f) {
		y = ((-0.9549476f * x - 1.37418593f) * x + 2.09137015f) * x - 0.16748867f;
	} else {
		y = ((3.0817580f * x - 5.87338670f) * x + 3.75112997f) * x - 0.37001483f;
	}

	*cx = x * 65536.0f + 0.5f;
	*cy = y * 65536.0f + 0.5f;
}

/*
 * The color temperature in mireds of CurrentX and CurrentY by the formula of McCamy (1992), within
 * min to max. y is never 0.1858 exactly, so n is finite.
 */
static uint16_t xy_temperature(uint16_t cx, uint16_t cy, uint16_t min, uint16_t max)
{
	float n = (cx / 65536.0f - 0.3320f) / (0.1858f - cy / 65536.0f);
	float kelvins = ((449.0f * n + 3525.0f) * n + 6823.3f) * n + 5520.33f;

	return CLAMP(1e6f / kelvins + 0.5f, min, max);
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_COLOR, attr, value);
}

/* With the XY capability, CurrentX and CurrentY follow ColorTemperatureMireds in its mode */
static bool follows(const struct zzb_zcl_color *v)
{
	return (v->color_capabilities & ZZB_ZCL_COLOR_CAP_XY) &&
	       v->color_mode == ZZB_ZCL_COLOR_MODE_TEMPERATURE;
}

/* CurrentX and CurrentY follow without being saved; the start hook computes them again */
static void follow(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_color *v)
{
	uint16_t x, y;

	if (!follows(v)) {
		return;
	}

	temperature_xy(v->color_temperature, &x, &y);
	zzb_zcl_update(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_X, x);
	zzb_zcl_update(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_Y, y);
}

/* The Color Control server on ep, or NULL */
static const struct zzb_zcl_cluster *server(const struct zzb_endpoint *ep)
{
	return zzb_zcl_find_cluster(ep, ZZB_ZCL_COLOR, true);
}

/* The values of the On/Off server on ep, or NULL */
static const struct zzb_zcl_on_off *on_off_server(struct zzb_node *node, uint8_t ep)
{
	const struct zzb_zcl_cluster *cl =
		zzb_zcl_find_cluster(zzb_node_endpoint(node, ep), ZZB_ZCL_ON_OFF, true);

	return cl != NULL ? cl->values : NULL;
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

static void move_mireds(struct zzb_node *node, uint8_t ep, struct zzb_zcl_color *v, uint16_t mireds,
			uint32_t time_ds)
{
	const uint16_t attr = ZZB_ZCL_COLOR_TEMPERATURE;

	move(node, ep, v, &attr, &mireds, 1, time_ds);
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

/*
 * Color temperature mode. From XY mode the temperature starts at that of the current x and y
 * within the physical range (ZCL 5.2.2.3.2).
 */
static void temperature_mode(struct zzb_node *node, uint8_t ep, struct zzb_zcl_color *v)
{
	if (v->color_mode == ZZB_ZCL_COLOR_MODE_TEMPERATURE) {
		return;
	}

	set_mode(node, ep, ZZB_ZCL_COLOR_MODE_TEMPERATURE);
	set(node, ep, ZZB_ZCL_COLOR_TEMPERATURE,
	    xy_temperature(v->current_x, v->current_y, v->color_temp_physical_min,
			   v->color_temp_physical_max));
	follow(node, ep, v);
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

/* ZCL 5.2.2.3.14: the target is limited to the physical range (ZCL 5.2.2.3.1) */
static int move_to_temperature(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			       struct zzb_zcl_color *v)
{
	uint16_t mireds = CLAMP(sys_get_le16(cmd->payload), v->color_temp_physical_min,
				v->color_temp_physical_max);

	temperature_mode(node, cmd->ep, v);
	move_mireds(node, cmd->ep, v, mireds, sys_get_le16(cmd->payload + 2));
	return ZZB_ZCL_SUCCESS;
}

/* The lower limit of a move or step: the Minimum Mireds field within the physical range */
static uint16_t min_mireds(const struct zzb_zcl_color *v, uint16_t field)
{
	return MAX(field, v->color_temp_physical_min);
}

/* The upper limit: the Maximum Mireds field within the physical range, 0 for its maximum */
static uint16_t max_mireds(const struct zzb_zcl_color *v, uint16_t field)
{
	return field == 0 ? v->color_temp_physical_max : MIN(field, v->color_temp_physical_max);
}

/* ZCL 5.2.2.3.21: the rate is in mireds per second, and stop mode stops the transition */
static int move_temperature(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			    struct zzb_zcl_color *v)
{
	uint8_t mode = cmd->payload[0];
	uint16_t rate = sys_get_le16(cmd->payload + 1);
	uint16_t mireds = mode == MODE_UP ? max_mireds(v, sys_get_le16(cmd->payload + 5))
					  : min_mireds(v, sys_get_le16(cmd->payload + 3));

	if (mode == MODE_STOP) {
		stop(node, cmd->ep, v);
		return ZZB_ZCL_SUCCESS;
	}

	if ((mode != MODE_UP && mode != MODE_DOWN) || rate == 0) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	temperature_mode(node, cmd->ep, v);
	move_mireds(node, cmd->ep, v, mireds, 10 * abs(mireds - v->color_temperature) / rate);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 5.2.2.3.22: a step cut at a limit takes the same part of the transition time */
static int step_temperature(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			    struct zzb_zcl_color *v)
{
	uint8_t mode = cmd->payload[0];
	uint16_t size = sys_get_le16(cmd->payload + 1);
	uint32_t time = sys_get_le16(cmd->payload + 3);
	uint16_t mireds;

	if (mode != MODE_UP && mode != MODE_DOWN) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	temperature_mode(node, cmd->ep, v);
	mireds = CLAMP(v->color_temperature + (mode == MODE_UP ? size : -size),
		       min_mireds(v, sys_get_le16(cmd->payload + 5)),
		       max_mireds(v, sys_get_le16(cmd->payload + 7)));
	move_mireds(node, cmd->ep, v, mireds,
		    size == 0 ? 0 : time * abs(mireds - v->color_temperature) / size);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 5.2.2.3.20 */
static int stop_move_step(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			  struct zzb_zcl_color *v)
{
	stop(node, cmd->ep, v);
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
	{ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE, 4, ZZB_ZCL_COLOR_CAP_TEMPERATURE, move_to_temperature},
	{ZZB_ZCL_COLOR_MOVE_TEMPERATURE, 7, ZZB_ZCL_COLOR_CAP_TEMPERATURE, move_temperature},
	{ZZB_ZCL_COLOR_STEP_TEMPERATURE, 9, ZZB_ZCL_COLOR_CAP_TEMPERATURE, step_temperature},
	{ZZB_ZCL_COLOR_STOP_MOVE_STEP, 0, ZZB_ZCL_COLOR_CAP_XY | ZZB_ZCL_COLOR_CAP_TEMPERATURE,
	 stop_move_step},
};

/*
 * ZCL 5.2.2.2.1.10: while the On/Off server on the endpoint is off, a command runs only with
 * ExecuteIfOff in its options, in which OptionsMask after the len octets of the other fields
 * selects the bits of OptionsOverride (ZCL 5.2.2.3.3)
 */
static bool executes(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		     const struct zzb_zcl_color *v, size_t len)
{
	const struct zzb_zcl_on_off *on_off = on_off_server(node, cmd->ep);
	const uint8_t *p = cmd->payload + len;
	uint8_t options = v->options;

	if (cmd->len >= len + 2) {
		options = (options & ~p[0]) | (p[1] & p[0]);
	}

	return on_off == NULL || on_off->on_off || (options & EXECUTE_IF_OFF);
}

/* The steps of a transition do not change SceneValid, so a command sets it FALSE at the start */
static void invalidate(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_transition *t)
{
	for (int i = 0; i < t->count; i++) {
		if (t->from[i] != t->to[i]) {
			zzb_zcl_scenes_invalidate(node, ep, true);
			return;
		}
	}
}

/* A command of a capability that the light lacks is unsupported (ZCL 5.2.2.2.1.18) */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	struct zzb_zcl_color *v = values;
	int status;

	ARRAY_FOR_EACH(commands, i) {
		if (commands[i].id != cmd->id ||
		    !(v->color_capabilities & commands[i].capability)) {
			continue;
		}

		if (cmd->len < commands[i].len) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		if (!executes(node, cmd, v, commands[i].len)) {
			return ZZB_ZCL_SUCCESS;
		}

		/* A command that succeeds replaces or stops the transition */
		status = commands[i].fn(node, cmd, v);
		if (status == ZZB_ZCL_SUCCESS) {
			invalidate(node, cmd->ep, &v->transition);
		}

		return status;
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

/*
 * ZCL 5.2.2.5: with the XY capability X and Y, which hold the color temperature, move in XY mode;
 * otherwise ColorTemperatureMireds within the physical range
 */
static void recall(struct zzb_node *node, uint8_t ep, void *values, const uint32_t *set_values,
		   size_t count, uint32_t transition_ds)
{
	struct zzb_zcl_color *v = values;

	if ((v->color_capabilities & ZZB_ZCL_COLOR_CAP_XY) && count >= 2) {
		set_mode(node, ep, ZZB_ZCL_COLOR_MODE_XY);
		move_xy(node, ep, v, set_values[0], set_values[1], transition_ds);
		return;
	}

	if (count < ARRAY_SIZE(scene_attrs)) {
		return;
	}

	/* ColorTemperatureMireds is the last attribute of the set */
	temperature_mode(node, ep, v);
	move_mireds(node, ep, v,
		    CLAMP(set_values[ARRAY_SIZE(scene_attrs) - 1], v->color_temp_physical_min,
			  v->color_temp_physical_max),
		    transition_ds);
}

/* CurrentX and CurrentY in line with the loaded ColorTemperatureMireds, without zcl_cb */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_zcl_color *v = values;
	uint16_t x, y;

	if (!follows(v)) {
		return;
	}

	temperature_xy(v->color_temperature, &x, &y);
	zzb_zcl_store(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_X, x);
	zzb_zcl_store(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_CURRENT_Y, y);
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	if (attr == ZZB_ZCL_COLOR_TEMPERATURE) {
		follow(node, ep, values);
	}
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_color *v = values;

	v->transition.count = 0;
	remaining(node, ep, v);
	schedule(node);
	set_mode(node, ep, default_mode(v));
	set(node, ep, ZZB_ZCL_COLOR_TEMPERATURE, default_temperature(v));
	if (follows(v)) {
		follow(node, ep, v);
	} else {
		set(node, ep, ZZB_ZCL_COLOR_CURRENT_X, DEFAULT_X);
		set(node, ep, ZZB_ZCL_COLOR_CURRENT_Y, DEFAULT_Y);
	}

	set(node, ep, ZZB_ZCL_COLOR_OPTIONS, 0);
	set(node, ep, ZZB_ZCL_COLOR_START_UP_TEMPERATURE, v->start_up_default);
}

const struct zzb_zcl_cluster_def zzb_zcl_color_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.scene_attrs = scene_attrs,
	.scene_attr_count = ARRAY_SIZE(scene_attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.cmd = cmd,
	.changed = changed,
	.recall = recall,
};

void zzb_zcl_color_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.color_timer, tick);
}
