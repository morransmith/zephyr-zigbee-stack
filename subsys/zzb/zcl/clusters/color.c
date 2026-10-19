/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/color.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 5.2.1.1) */
#define REVISION 3

/* The highest CurrentX and CurrentY (ZCL 5.2.2.2.1.4) */
#define XY_MAX 0xfeff

/* The defaults of CurrentX, CurrentY and ColorTemperatureMireds (ZCL Table 5.3) */
#define DEFAULT_X           0x616b
#define DEFAULT_Y           0x607d
#define DEFAULT_TEMPERATURE 0x00fa

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
	set(node, cmd->ep, ZZB_ZCL_COLOR_CURRENT_X, MIN(sys_get_le16(cmd->payload), XY_MAX));
	set(node, cmd->ep, ZZB_ZCL_COLOR_CURRENT_Y, MIN(sys_get_le16(cmd->payload + 2), XY_MAX));
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
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_color *v = values;

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
