/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/illuminance.h>

#include "zcl/zcl.h"

/* ClusterRevision of Illuminance Measurement (ZCL 4.2.1.1) */
#define REVISION 2

/* ClusterRevision of Illuminance Level Sensing (ZCL 4.3.1.1) */
#define LEVEL_REVISION 1

/* IlluminanceTargetLevel that is not valid (ZCL 4.3.2.4.1), the default */
#define TARGET_NOT_VALID 0xffff

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_MEASURED_VALUE, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_illuminance, measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_MIN_MEASURED_VALUE, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_illuminance, min_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_MAX_MEASURED_VALUE, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_illuminance, max_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_illuminance,
		     tolerance),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_LIGHT_SENSOR_TYPE, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_illuminance, light_sensor_type),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_illuminance_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static const struct zzb_zcl_attr level_attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_LEVEL_STATUS, ZZB_ZCL_ENUM8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_illuminance_level, level_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_LEVEL_LIGHT_SENSOR_TYPE, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_illuminance_level, light_sensor_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_illuminance_level,
		     illuminance_target_level),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, LEVEL_REVISION),
};

static void level_init(void *values)
{
	struct zzb_zcl_illuminance_level *v = values;

	v->illuminance_target_level = TARGET_NOT_VALID;
}

static void level_reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_ILLUMINANCE_LEVEL, ZZB_ZCL_ILLUMINANCE_LEVEL_TARGET_LEVEL,
		    TARGET_NOT_VALID);
}

const struct zzb_zcl_cluster_def zzb_zcl_illuminance_level_def = {
	.attrs = level_attrs,
	.attr_count = ARRAY_SIZE(level_attrs),
	.init = level_init,
	.reset = level_reset,
};
