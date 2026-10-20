/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/ota.h>

#include "aps/aps.h"
#include "common/node.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 11.3.1.1) */
#define REVISION 4

/* CurrentZigBeeStackVersion of ZigBee PRO (ZCL Table 11-6) */
#define STACK_PRO 0x0002

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_UPGRADE_SERVER_ID, ZZB_ZCL_EUI64, 0, struct zzb_zcl_ota,
		     upgrade_server_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_FILE_OFFSET, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota, file_offset),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_CURRENT_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     current_file_version),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_OTA_CURRENT_STACK_VERSION, ZZB_ZCL_UINT16, STACK_PRO),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     downloaded_file_version),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_ota,
		     image_upgrade_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_MANUFACTURER_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     manufacturer_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_TYPE_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     image_type_id),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The defaults outside a download (ZCL Table 11-10); the application sets the running image */
static void init(void *values)
{
	struct zzb_zcl_ota *v = values;

	v->file_offset = 0xffffffff;
	v->downloaded_file_version = 0xffffffff;
	v->image_upgrade_status = ZZB_ZCL_OTA_NORMAL;
	v->image_type_id = 0xffff;
}

static void refresh(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_ota *v = values;

	v->upgrade_server_id = zzb_apsme_get_tc_addr(node);
	v->manufacturer_id = node->device->manufacturer_code;
}

const struct zzb_zcl_cluster_def zzb_zcl_ota_client_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.refresh = refresh,
};
