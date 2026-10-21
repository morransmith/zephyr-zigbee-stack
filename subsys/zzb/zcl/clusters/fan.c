/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/fan.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 6.4.1.1) */
#define REVISION 1

/* ZCL does not mark FanMode reportable, but ZHA 2.3.1 configures its report */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_FAN_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct zzb_zcl_fan, fan_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_FAN_MODE_SEQUENCE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_fan,
		     fan_mode_sequence),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	if (attr->id != ZZB_ZCL_FAN_MODE) {
		return ZZB_ZCL_SUCCESS;
	}

	return value <= ZZB_ZCL_FAN_MODE_SMART ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

static void init(void *values)
{
	struct zzb_zcl_fan *v = values;

	v->fan_mode = ZZB_ZCL_FAN_MODE_AUTO;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_FAN, ZZB_ZCL_FAN_MODE, ZZB_ZCL_FAN_MODE_AUTO);
}

const struct zzb_zcl_cluster_def zzb_zcl_fan_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_fan_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
