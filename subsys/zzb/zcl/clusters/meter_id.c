/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/meter_id.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 10.13.1.1) */
#define REVISION 2

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_COMPANY_NAME, ZZB_ZCL_STRING, 0, struct zzb_zcl_meter_id,
		     company_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_METER_TYPE_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_meter_id,
		     meter_type_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_DATA_QUALITY_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_meter_id,
		     data_quality_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_CUSTOMER_NAME, ZZB_ZCL_STRING, 0, struct zzb_zcl_meter_id,
		     customer_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_MODEL, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_meter_id, model),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_PART_NUMBER, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_meter_id,
		     part_number),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_PRODUCT_REVISION, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_meter_id,
		     product_revision),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_SOFTWARE_REVISION, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_meter_id,
		     software_revision),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_UTILITY_NAME, ZZB_ZCL_STRING, 0, struct zzb_zcl_meter_id,
		     utility_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_POD, ZZB_ZCL_STRING, 0, struct zzb_zcl_meter_id, pod),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_AVAILABLE_POWER, ZZB_ZCL_INT24, 0, struct zzb_zcl_meter_id,
		     available_power),
	ZZB_ZCL_ATTR(ZZB_ZCL_METER_ID_POWER_THRESHOLD, ZZB_ZCL_INT24, 0, struct zzb_zcl_meter_id,
		     power_threshold),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_meter_id_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_meter_id_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
