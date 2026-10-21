/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/on_off_switch_config.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.9.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_TYPE, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_on_off_switch_config, switch_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_on_off_switch_config,
		     switch_actions),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* SwitchActions is the only writable attribute; Toggle is its highest value */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	if (value > ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS_TOGGLE) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

static void init(void *values)
{
	struct zzb_zcl_on_off_switch_config *v = values;

	v->switch_actions = ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS_ON_OFF;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_ON_OFF_SWITCH_CONFIG,
		    ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS,
		    ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SWITCH_ACTIONS_ON_OFF);
}

const struct zzb_zcl_cluster_def zzb_zcl_on_off_switch_config_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
};
