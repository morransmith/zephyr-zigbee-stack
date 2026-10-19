/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/basic.h>

/* ZCLVersion of ZCL rev 8 (ZCL 3.2.2.2.1) */
#define ZCL_VERSION 8

/* ClusterRevision (ZCL 3.2.1.1) */
#define REVISION 3

/* The attributes that the DTL requires for the On/Off Light */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR_CONST(0x0000, ZZB_ZCL_UINT8, ZCL_VERSION),
	ZZB_ZCL_ATTR(0x0001, ZZB_ZCL_UINT8, 0, struct zzb_zcl_basic, app_version),
	ZZB_ZCL_ATTR(0x0002, ZZB_ZCL_UINT8, 0, struct zzb_zcl_basic, stack_version),
	ZZB_ZCL_ATTR(0x0003, ZZB_ZCL_UINT8, 0, struct zzb_zcl_basic, hw_version),
	ZZB_ZCL_ATTR(0x0004, ZZB_ZCL_STRING, 0, struct zzb_zcl_basic, manufacturer_name),
	ZZB_ZCL_ATTR(0x0005, ZZB_ZCL_STRING, 0, struct zzb_zcl_basic, model_id),
	ZZB_ZCL_ATTR(0x0006, ZZB_ZCL_STRING, 0, struct zzb_zcl_basic, date_code),
	ZZB_ZCL_ATTR(0x0007, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_basic, power_source),
	ZZB_ZCL_ATTR(0x0008, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_basic, generic_device_class),
	ZZB_ZCL_ATTR(0x0009, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_basic, generic_device_type),
	ZZB_ZCL_ATTR(0x000a, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_basic, product_code),
	ZZB_ZCL_ATTR(0x000b, ZZB_ZCL_STRING, 0, struct zzb_zcl_basic, product_url),
	ZZB_ZCL_ATTR(0x4000, ZZB_ZCL_STRING, 0, struct zzb_zcl_basic, sw_build_id),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_basic_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};
