/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_ID_H_
#define ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_ID_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EN50523 Appliance Identification cluster (ZCL 15.3). */
#define ZZB_ZCL_APPLIANCE_ID 0x0b00

/** Attributes (ZCL Tables 15-12 and 15-15). */
#define ZZB_ZCL_APPLIANCE_ID_BASIC_IDENTIFICATION        0x0000
#define ZZB_ZCL_APPLIANCE_ID_COMPANY_NAME                0x0010
#define ZZB_ZCL_APPLIANCE_ID_COMPANY_ID                  0x0011
#define ZZB_ZCL_APPLIANCE_ID_BRAND_NAME                  0x0012
#define ZZB_ZCL_APPLIANCE_ID_BRAND_ID                    0x0013
#define ZZB_ZCL_APPLIANCE_ID_MODEL                       0x0014
#define ZZB_ZCL_APPLIANCE_ID_PART_NUMBER                 0x0015
#define ZZB_ZCL_APPLIANCE_ID_PRODUCT_REVISION            0x0016
#define ZZB_ZCL_APPLIANCE_ID_SOFTWARE_REVISION           0x0017
#define ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_NAME           0x0018
#define ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_ID             0x0019
#define ZZB_ZCL_APPLIANCE_ID_CECED_SPECIFICATION_VERSION 0x001a

/**
 * Values of an Appliance Identification server (ZCL 15.3.2). The application fills them before
 * zzb_node_init() and changes the integers with zzb_zcl_attr_set(); they are read-only. A NULL
 * string reads as an empty string.
 */
struct zzb_zcl_appliance_id {
	/**
	 * Company ID in bits 0 to 15, Brand ID in bits 16 to 31, Product Type ID in bits 32 to 47
	 * and the specification version in bits 48 to 55 (ZCL Table 15-13).
	 */
	uint64_t basic_identification;
	/** Up to 16 characters. */
	const char *company_name;
	uint16_t company_id;
	/** Up to 16 characters. */
	const char *brand_name;
	uint16_t brand_id;
	/** Octet strings, pointers to their length octet: up to 16 octets. */
	const uint8_t *model;
	const uint8_t *part_number;
	/** Up to 6 octets. */
	const uint8_t *product_revision;
	const uint8_t *software_revision;
	/** 2 octets, such as "WM" for a washing machine. */
	const uint8_t *product_type_name;
	/** ZCL Table 15-14. */
	uint16_t product_type_id;
	/** ZCL Table 15-16, such as 0x10 for v1.0 not certified. */
	uint8_t ceced_specification_version;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_id_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_id_client_def;

/**
 * An Appliance Identification server with the values v, a pointer to struct zzb_zcl_appliance_id,
 * which the White Goods device has (DTL).
 */
#define ZZB_ZCL_APPLIANCE_ID_SERVER(v)                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_ID,                                                        \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_appliance_id *)NULL),                         \
		.def = &zzb_zcl_appliance_id_def,                                                  \
	}

/** An Appliance Identification client; it accepts reports. */
#define ZZB_ZCL_APPLIANCE_ID_CLIENT()                                                              \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_ID,                                                        \
		.server = false,                                                                   \
		.def = &zzb_zcl_appliance_id_client_def,                                           \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_ID_H_ */
