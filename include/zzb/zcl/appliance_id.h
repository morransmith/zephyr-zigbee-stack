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

/** Fields of BasicIdentification (ZCL Table 15-13). */
#define ZZB_ZCL_APPLIANCE_ID_BASIC_COMPANY_ID      GENMASK64(15, 0)
#define ZZB_ZCL_APPLIANCE_ID_BASIC_BRAND_ID        GENMASK64(31, 16)
#define ZZB_ZCL_APPLIANCE_ID_BASIC_PRODUCT_TYPE_ID GENMASK64(47, 32)
#define ZZB_ZCL_APPLIANCE_ID_BASIC_SPEC_VERSION    GENMASK64(55, 48)

/** ProductTypeId and the Product Type ID of BasicIdentification (ZCL Table 15-14). */
enum zzb_zcl_appliance_id_product_type {
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_WHITE_GOODS = 0x0000,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_DISHWASHER = 0x5601,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_TUMBLE_DRYER = 0x5602,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_WASHER_DRYER = 0x5603,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_WASHING_MACHINE = 0x5604,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_HOBS = 0x5e03,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_INDUCTION_HOBS = 0x5e09,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_OVEN = 0x5e01,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_ELECTRICAL_OVEN = 0x5e06,
	ZZB_ZCL_APPLIANCE_ID_PRODUCT_TYPE_REFRIGERATOR_FREEZER = 0x6601,
};

/** Fields of CECEDSpecificationVersion: X of vX.0 and the certification (ZCL Table 15-16). */
#define ZZB_ZCL_APPLIANCE_ID_CECED_VERSION       GENMASK(7, 4)
#define ZZB_ZCL_APPLIANCE_ID_CECED_CERTIFICATION GENMASK(3, 0)

/** ZZB_ZCL_APPLIANCE_ID_CECED_CERTIFICATION (ZCL Table 15-16). */
enum zzb_zcl_appliance_id_ceced_certification {
	ZZB_ZCL_APPLIANCE_ID_CECED_NOT_CERTIFIED = 0x0,
	ZZB_ZCL_APPLIANCE_ID_CECED_CERTIFIED = 0xa,
};

/**
 * Values of an Appliance Identification server (ZCL 15.3.2). The application fills them before
 * zzb_node_init() and changes the integers with zzb_zcl_attr_set(); they are read-only. A NULL
 * string reads as an empty string.
 */
struct zzb_zcl_appliance_id {
	/** ZZB_ZCL_APPLIANCE_ID_BASIC_* fields. */
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
	/** enum zzb_zcl_appliance_id_product_type. */
	uint16_t product_type_id;
	/** ZZB_ZCL_APPLIANCE_ID_CECED_VERSION and _CERTIFICATION fields. */
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
