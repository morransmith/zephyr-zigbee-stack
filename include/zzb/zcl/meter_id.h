/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_METER_ID_H_
#define ZZB_INCLUDE_ZZB_ZCL_METER_ID_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Meter Identification cluster (ZCL 10.13). */
#define ZZB_ZCL_METER_ID 0x0b01

/** Attributes (ZCL Table 10-215). */
#define ZZB_ZCL_METER_ID_COMPANY_NAME      0x0000
#define ZZB_ZCL_METER_ID_METER_TYPE_ID     0x0001
#define ZZB_ZCL_METER_ID_DATA_QUALITY_ID   0x0004
#define ZZB_ZCL_METER_ID_CUSTOMER_NAME     0x0005
#define ZZB_ZCL_METER_ID_MODEL             0x0006
#define ZZB_ZCL_METER_ID_PART_NUMBER       0x0007
#define ZZB_ZCL_METER_ID_PRODUCT_REVISION  0x0008
#define ZZB_ZCL_METER_ID_SOFTWARE_REVISION 0x000a
#define ZZB_ZCL_METER_ID_UTILITY_NAME      0x000b
#define ZZB_ZCL_METER_ID_POD               0x000c
#define ZZB_ZCL_METER_ID_AVAILABLE_POWER   0x000d
#define ZZB_ZCL_METER_ID_POWER_THRESHOLD   0x000e

/** MeterTypeID (ZCL Table 10-216). */
enum zzb_zcl_meter_id_type {
	ZZB_ZCL_METER_ID_TYPE_UTILITY_PRIMARY = 0x0000,
	ZZB_ZCL_METER_ID_TYPE_UTILITY_PRODUCTION = 0x0001,
	ZZB_ZCL_METER_ID_TYPE_UTILITY_SECONDARY = 0x0002,
	ZZB_ZCL_METER_ID_TYPE_PRIVATE_PRIMARY = 0x0100,
	ZZB_ZCL_METER_ID_TYPE_PRIVATE_PRODUCTION = 0x0101,
	ZZB_ZCL_METER_ID_TYPE_PRIVATE_SECONDARY = 0x0102,
	ZZB_ZCL_METER_ID_TYPE_GENERIC = 0x0110,
};

/** DataQualityID (ZCL Table 10-217). */
enum zzb_zcl_meter_id_data_quality {
	ZZB_ZCL_METER_ID_DATA_QUALITY_ALL_CERTIFIED = 0x0000,
	/** All data except the instantaneous power is certified. */
	ZZB_ZCL_METER_ID_DATA_QUALITY_POWER_NOT_CERTIFIED = 0x0001,
	/** All data except the cumulated consumption is certified. */
	ZZB_ZCL_METER_ID_DATA_QUALITY_CONSUMPTION_NOT_CERTIFIED = 0x0002,
	ZZB_ZCL_METER_ID_DATA_QUALITY_NOT_CERTIFIED = 0x0003,
};

/**
 * Values of a Meter Identification server (ZCL 10.13.2.1), all read-only and set by the
 * application. CustomerName is read-only although ZCL makes it writable, since the strings of the
 * stack are. A NULL string reads as an empty string.
 */
struct zzb_zcl_meter_id {
	/** Up to 16 characters. */
	const char *company_name;
	/** enum zzb_zcl_meter_id_type. */
	uint16_t meter_type_id;
	/** enum zzb_zcl_meter_id_data_quality. */
	uint16_t data_quality_id;
	/** Up to 16 ASCII characters. */
	const char *customer_name;
	/** The length octet and up to 16 octets of UTF-8 text. */
	const uint8_t *model;
	const uint8_t *part_number;
	/** The length octet and up to 6 octets of UTF-8 text. */
	const uint8_t *product_revision;
	const uint8_t *software_revision;
	/** Up to 16 ASCII characters. */
	const char *utility_name;
	/** The point of delivery, up to 16 ASCII characters. */
	const char *pod;
	/** 24 bits with sign, in the unit and format of InstantaneousDemand of Metering. */
	int32_t available_power;
	int32_t power_threshold;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_meter_id_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_meter_id_client_def;

/** A Meter Identification server with the values v, a pointer to struct zzb_zcl_meter_id. */
#define ZZB_ZCL_METER_ID_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_METER_ID,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_meter_id *)NULL),                             \
		.def = &zzb_zcl_meter_id_def,                                                      \
	}

/** A Meter Identification client; it accepts reports. */
#define ZZB_ZCL_METER_ID_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_METER_ID,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_meter_id_client_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_METER_ID_H_ */
