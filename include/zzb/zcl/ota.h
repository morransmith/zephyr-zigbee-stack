/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_OTA_H_
#define ZZB_INCLUDE_ZZB_ZCL_OTA_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** OTA Upgrade cluster (ZCL 11). */
#define ZZB_ZCL_OTA 0x0019

/** Attributes of the client (ZCL Table 11-10). */
#define ZZB_ZCL_OTA_UPGRADE_SERVER_ID       0x0000
#define ZZB_ZCL_OTA_FILE_OFFSET             0x0001
#define ZZB_ZCL_OTA_CURRENT_FILE_VERSION    0x0002
#define ZZB_ZCL_OTA_CURRENT_STACK_VERSION   0x0003
#define ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION 0x0004
#define ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS    0x0006
#define ZZB_ZCL_OTA_MANUFACTURER_ID         0x0007
#define ZZB_ZCL_OTA_IMAGE_TYPE_ID           0x0008

/** Values of ImageUpgradeStatus (ZCL Table 11-11). */
#define ZZB_ZCL_OTA_NORMAL            0x00
#define ZZB_ZCL_OTA_DOWNLOADING       0x01
#define ZZB_ZCL_OTA_DOWNLOAD_COMPLETE 0x02
#define ZZB_ZCL_OTA_WAITING           0x03
#define ZZB_ZCL_OTA_COUNT_DOWN        0x04

/** Commands that the client generates (ZCL Table 11-16). */
#define ZZB_ZCL_OTA_QUERY_NEXT_IMAGE 0x01
#define ZZB_ZCL_OTA_IMAGE_BLOCK      0x03
#define ZZB_ZCL_OTA_UPGRADE_END      0x06

/** Commands that the client receives (ZCL Table 11-16). */
#define ZZB_ZCL_OTA_IMAGE_NOTIFY         0x00
#define ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP 0x02
#define ZZB_ZCL_OTA_IMAGE_BLOCK_RSP      0x05
#define ZZB_ZCL_OTA_UPGRADE_END_RSP      0x07

/**
 * Values of an OTA Upgrade client (ZCL 11.10), which downloads firmware images from the trust
 * center into the upload slot of MCUboot. The application fills current_file_version and
 * image_type before zzb_node_init(); the stack keeps the other fields. All attributes are
 * read-only. The client on the first endpoint with one downloads.
 */
struct zzb_zcl_ota {
	/** CurrentFileVersion of the running image, e.g. APPVERSION of the VERSION file. */
	uint32_t current_file_version;
	/** Image type of the device, 0x0000 to 0xffbf. */
	uint16_t image_type;
	/** apsTrustCenterAddress, written before a read. */
	uint64_t upgrade_server_id;
	/** Offset of the next block of a download; 0xffffffff outside one. */
	uint32_t file_offset;
	/** File version of the image of a download; 0xffffffff without one. */
	uint32_t downloaded_file_version;
	/** ZZB_ZCL_OTA_NORMAL to ZZB_ZCL_OTA_COUNT_DOWN. */
	uint8_t image_upgrade_status;
	/** Manufacturer code of the device definition, written before a read. */
	uint16_t manufacturer_id;
	/** image_type during a download; 0xffff otherwise. */
	uint16_t image_type_id;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ota_client_def;

/**
 * An OTA Upgrade client with the values v, a pointer to struct zzb_zcl_ota. Without
 * CONFIG_ZZB_ZCL_OTA the definition fails to link.
 */
#define ZZB_ZCL_OTA_CLIENT(v)                                                                      \
	{                                                                                          \
		.id = ZZB_ZCL_OTA,                                                                 \
		.server = false,                                                                   \
		.values = (1 ? (v) : (struct zzb_zcl_ota *)NULL),                                  \
		.def = &zzb_zcl_ota_client_def,                                                    \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_OTA_H_ */
