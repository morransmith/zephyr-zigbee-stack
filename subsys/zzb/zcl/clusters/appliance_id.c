/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/appliance_id.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 15.3.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_BASIC_IDENTIFICATION, ZZB_ZCL_UINT56, 0,
		     struct zzb_zcl_appliance_id, basic_identification),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_COMPANY_NAME, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_appliance_id, company_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_COMPANY_ID, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_appliance_id, company_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_BRAND_NAME, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_appliance_id, brand_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_BRAND_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_appliance_id,
		     brand_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_MODEL, ZZB_ZCL_OCTSTR, 0, struct zzb_zcl_appliance_id,
		     model),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_PART_NUMBER, ZZB_ZCL_OCTSTR, 0,
		     struct zzb_zcl_appliance_id, part_number),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_PRODUCT_REVISION, ZZB_ZCL_OCTSTR, 0,
		     struct zzb_zcl_appliance_id, product_revision),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_SOFTWARE_REVISION, ZZB_ZCL_OCTSTR, 0,
		     struct zzb_zcl_appliance_id, software_revision),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_NAME, ZZB_ZCL_OCTSTR, 0,
		     struct zzb_zcl_appliance_id, product_type_name),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_ID, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_appliance_id, product_type_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_ID_CECED_SPECIFICATION_VERSION, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_appliance_id, ceced_specification_version),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_appliance_id_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_appliance_id_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
