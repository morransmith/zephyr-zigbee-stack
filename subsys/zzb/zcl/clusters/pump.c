/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/pump.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 6.2.1.1) */
#define REVISION 1

/*
 * ZCL Table 6-4 and its text disagree on the access of Power and LifetimeEnergyConsumed; both are
 * read-only measurements of the application
 */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_PRESSURE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     max_pressure),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_SPEED, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump, max_speed),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_FLOW, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump, max_flow),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MIN_CONST_PRESSURE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     min_const_pressure),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_CONST_PRESSURE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     max_const_pressure),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MIN_COMP_PRESSURE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     min_comp_pressure),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_COMP_PRESSURE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     max_comp_pressure),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MIN_CONST_SPEED, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump,
		     min_const_speed),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_CONST_SPEED, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump,
		     max_const_speed),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MIN_CONST_FLOW, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump,
		     min_const_flow),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_CONST_FLOW, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump,
		     max_const_flow),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MIN_CONST_TEMP, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     min_const_temp),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_MAX_CONST_TEMP, ZZB_ZCL_INT16, 0, struct zzb_zcl_pump,
		     max_const_temp),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_STATUS, ZZB_ZCL_MAP16, ZZB_ZCL_REPORT, struct zzb_zcl_pump,
		     pump_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_EFFECTIVE_OPERATION_MODE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_pump,
		     effective_operation_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_EFFECTIVE_CONTROL_MODE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_pump,
		     effective_control_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_CAPACITY, ZZB_ZCL_INT16, ZZB_ZCL_REPORT, struct zzb_zcl_pump,
		     capacity),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_SPEED, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pump, speed),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_LIFETIME_RUNNING_HOURS, ZZB_ZCL_UINT24,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_pump, lifetime_running_hours),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_POWER, ZZB_ZCL_UINT24, 0, struct zzb_zcl_pump, power),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_LIFETIME_ENERGY_CONSUMED, ZZB_ZCL_UINT32, ZZB_ZCL_NV,
		     struct zzb_zcl_pump, lifetime_energy_consumed),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_OPERATION_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_pump, operation_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_CONTROL_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_pump, control_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_PUMP_ALARM_MASK, ZZB_ZCL_MAP16, 0, struct zzb_zcl_pump, alarm_mask),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static uint8_t valid(bool cond)
{
	return cond ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

/* ZCL Tables 6-7 and 6-8 */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	switch (attr->id) {
	case ZZB_ZCL_PUMP_OPERATION_MODE:
		return valid(value <= 3);
	case ZZB_ZCL_PUMP_CONTROL_MODE:
		return valid(value <= 3 || value == 5 || value == 7);
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

static void init(void *values)
{
	struct zzb_zcl_pump *v = values;

	v->lifetime_running_hours = 0;
	v->operation_mode = 0;
	v->control_mode = 0;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_PUMP, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_PUMP_LIFETIME_RUNNING_HOURS, 0);
	set(node, ep, ZZB_ZCL_PUMP_OPERATION_MODE, 0);
	set(node, ep, ZZB_ZCL_PUMP_CONTROL_MODE, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_pump_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
};
