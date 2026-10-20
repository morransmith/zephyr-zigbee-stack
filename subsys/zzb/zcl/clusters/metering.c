/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/metering.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 10.4.1.1) */
#define REVISION 2

/*
 * DTL Table 75 makes InstantaneousDemand reportable; ZHA 2.3.1 also configures the reports of
 * CurrentSummationDelivered and Status
 */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED, ZZB_ZCL_UINT48, ZZB_ZCL_REPORT,
		     struct zzb_zcl_metering, current_summation_delivered),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_STATUS, ZZB_ZCL_MAP8, ZZB_ZCL_REPORT, struct zzb_zcl_metering,
		     status),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_UNIT_OF_MEASURE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_metering,
		     unit_of_measure),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_MULTIPLIER, ZZB_ZCL_UINT24, 0, struct zzb_zcl_metering,
		     multiplier),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_DIVISOR, ZZB_ZCL_UINT24, 0, struct zzb_zcl_metering, divisor),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_SUMMATION_FORMATTING, ZZB_ZCL_MAP8, 0,
		     struct zzb_zcl_metering, summation_formatting),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_DEMAND_FORMATTING, ZZB_ZCL_MAP8, 0, struct zzb_zcl_metering,
		     demand_formatting),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_DEVICE_TYPE, ZZB_ZCL_MAP8, 0, struct zzb_zcl_metering,
		     metering_device_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND, ZZB_ZCL_INT24, ZZB_ZCL_REPORT,
		     struct zzb_zcl_metering, instantaneous_demand),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_metering_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_metering_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
