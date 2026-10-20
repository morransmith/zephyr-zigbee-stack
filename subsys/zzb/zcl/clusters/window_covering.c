/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/window_covering.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 7.4.1.1) */
#define REVISION 3

/* The bits of Mode that ZCL Table 7-44 defines */
#define MODE_BITS 0x0f

/* WindowCoveringType (ZCL Table 7-41) */
#define SHUTTER                  0x06
#define TILT_BLIND_TILT_ONLY     0x07
#define TILT_BLIND_LIFT_AND_TILT 0x08

#define PERCENTAGE_MAX 100

/* VelocityLift, the acceleration and deceleration times and the setpoints are not implemented */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_TYPE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_window_covering,
		     window_covering_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_PHYSICAL_CLOSED_LIMIT_LIFT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, physical_closed_limit_lift),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_PHYSICAL_CLOSED_LIMIT_TILT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, physical_closed_limit_tilt),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_LIFT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, current_position_lift),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_CURRENT_POSITION_TILT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, current_position_tilt),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_NUMBER_OF_ACTUATIONS_LIFT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, number_of_actuations_lift),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_NUMBER_OF_ACTUATIONS_TILT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, number_of_actuations_tilt),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_CONFIG_STATUS, ZZB_ZCL_MAP8, 0,
		     struct zzb_zcl_window_covering, config_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_window_covering, current_position_lift_percentage),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_window_covering, current_position_tilt_percentage),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_LIFT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, installed_open_limit_lift),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_LIFT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, installed_closed_limit_lift),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_INSTALLED_OPEN_LIMIT_TILT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, installed_open_limit_tilt),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_INSTALLED_CLOSED_LIMIT_TILT, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_window_covering, installed_closed_limit_tilt),
	ZZB_ZCL_ATTR(ZZB_ZCL_WINDOW_COVERING_MODE, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_window_covering, mode),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 7.4.2.4 */
static const struct zzb_zcl_scene_attr scene_attrs[] = {
	{ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE, ZZB_ZCL_UINT8},
	{ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE, ZZB_ZCL_UINT8},
};

/* The axes of the type, as in ZHA 2.3.1 */
static bool lift(const struct zzb_zcl_window_covering *v)
{
	return v->window_covering_type != SHUTTER &&
	       v->window_covering_type != TILT_BLIND_TILT_ONLY;
}

static bool tilt(const struct zzb_zcl_window_covering *v)
{
	return v->window_covering_type >= SHUTTER &&
	       v->window_covering_type <= TILT_BLIND_LIFT_AND_TILT;
}

/* ZCL 7.4.2.2.5, 7.4.2.2.7 */
static uint8_t check_percentage(const struct zzb_zcl_cmd *cmd, bool axis)
{
	if (!axis) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	return cmd->payload[0] <= PERCENTAGE_MAX ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

/* ZCL 7.4.2.2.4 and 7.4.2.2.6 state the limits the other way round, so either order passes */
static uint8_t check_value(const struct zzb_zcl_cmd *cmd, bool axis, uint16_t open, uint16_t closed)
{
	uint16_t value;

	if (!axis) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	value = sys_get_le16(cmd->payload);
	if (value < MIN(open, closed) || value > MAX(open, closed)) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

/* The checks of the stack before zcl_cmd_cb */
static uint8_t check_cmd(const struct zzb_zcl_cmd *cmd, const struct zzb_zcl_window_covering *v)
{
	switch (cmd->id) {
	case ZZB_ZCL_WINDOW_COVERING_UP_OPEN:
	case ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE:
	case ZZB_ZCL_WINDOW_COVERING_STOP:
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_VALUE:
		return check_value(cmd, lift(v), v->installed_open_limit_lift,
				   v->installed_closed_limit_lift);
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
		return check_percentage(cmd, lift(v));
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_VALUE:
		return check_value(cmd, tilt(v), v->installed_open_limit_tilt,
				   v->installed_closed_limit_tilt);
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE:
		return check_percentage(cmd, tilt(v));
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	uint8_t status = check_cmd(cmd, values);

	if (status != ZZB_ZCL_SUCCESS) {
		return status;
	}

	return zzb_zcl_app_cmd(node, cmd);
}

/* Passes Go To Lift Percentage or Go To Tilt Percentage of a recalled scene to zcl_cmd_cb */
static void go_to(struct zzb_node *node, uint8_t ep, uint8_t id, uint8_t percentage)
{
	const struct zzb_zcl_cmd cmd = {
		.ep = ep,
		.cluster = ZZB_ZCL_WINDOW_COVERING,
		.id = id,
		.payload = &percentage,
		.len = 1,
		.server = true,
	};

	zzb_zcl_app_cmd(node, &cmd);
}

/* The motor moves at its own speed, so the transition time is ignored; 0xff is unknown */
static void recall(struct zzb_node *node, uint8_t ep, void *values, const uint32_t *set,
		   size_t count, uint32_t transition_ds)
{
	const struct zzb_zcl_window_covering *v = values;

	if (count > 0 && lift(v) && set[0] <= PERCENTAGE_MAX) {
		go_to(node, ep, ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE, set[0]);
	}

	if (count > 1 && tilt(v) && set[1] <= PERCENTAGE_MAX) {
		go_to(node, ep, ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE, set[1]);
	}
}

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	if (attr->id == ZZB_ZCL_WINDOW_COVERING_MODE && (value & ~MODE_BITS)) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

/* Mode 0 instead of 0x04 of ZCL Table 7-43, which puts the motor in maintenance mode */
static void init(void *values)
{
	struct zzb_zcl_window_covering *v = values;

	v->mode = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_WINDOW_COVERING, ZZB_ZCL_WINDOW_COVERING_MODE, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_window_covering_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.scene_attrs = scene_attrs,
	.scene_attr_count = ARRAY_SIZE(scene_attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
	.recall = recall,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_window_covering_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
