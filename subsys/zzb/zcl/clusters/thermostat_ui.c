/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/thermostat.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 6.6.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_UI_TEMPERATURE_DISPLAY_MODE, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat_ui,
		     temperature_display_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat_ui, keypad_lockout),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_UI_SCHEDULE_PROGRAMMING_VISIBILITY, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat_ui,
		     schedule_programming_visibility),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL Tables 6-50 to 6-52 */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	uint32_t max = attr->id == ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT ? 0x05 : 0x01;

	return value <= max ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

static void init(void *values)
{
	struct zzb_zcl_thermostat_ui *v = values;

	*v = (struct zzb_zcl_thermostat_ui){0};
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_THERMOSTAT_UI, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_THERMOSTAT_UI_TEMPERATURE_DISPLAY_MODE, 0);
	set(node, ep, ZZB_ZCL_THERMOSTAT_UI_KEYPAD_LOCKOUT, 0);
	set(node, ep, ZZB_ZCL_THERMOSTAT_UI_SCHEDULE_PROGRAMMING_VISIBILITY, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_thermostat_ui_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
};
