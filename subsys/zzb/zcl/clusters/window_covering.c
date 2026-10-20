/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/window_covering.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 7.4.1.1) */
#define REVISION 3

/* The bits of Mode that ZCL Table 7-44 defines */
#define MODE_BITS 0x0f

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

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_WINDOW_COVERING_UP_OPEN:
	case ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE:
	case ZZB_ZCL_WINDOW_COVERING_STOP:
		return zzb_zcl_app_cmd(node, cmd);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
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
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
};
