/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_BASIC_H_
#define ZZB_INCLUDE_ZZB_ZCL_BASIC_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Basic cluster (ZCL 3.2). */
#define ZZB_ZCL_BASIC 0x0000

/** The primary source of PowerSource, bits 0 to 6 (ZCL Table 3-8). */
enum zzb_zcl_basic_power_source {
	ZZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN = 0x00,
	ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE = 0x01,
	ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_3_PHASE = 0x02,
	ZZB_ZCL_BASIC_POWER_SOURCE_BATTERY = 0x03,
	ZZB_ZCL_BASIC_POWER_SOURCE_DC = 0x04,
	ZZB_ZCL_BASIC_POWER_SOURCE_EMERGENCY_MAINS_CONSTANT = 0x05,
	ZZB_ZCL_BASIC_POWER_SOURCE_EMERGENCY_MAINS_TRANSFER_SWITCH = 0x06,
};

/** Fields of PowerSource: the primary source and a battery backup (ZCL 3.2.2.2.8). */
#define ZZB_ZCL_BASIC_POWER_SOURCE_PRIMARY        GENMASK(6, 0)
#define ZZB_ZCL_BASIC_POWER_SOURCE_BATTERY_BACKUP BIT(7)

/** GenericDeviceClass (ZCL Table 3-9); UNSPECIFIED is the default of ZCL Table 3-7. */
enum zzb_zcl_basic_generic_device_class {
	ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_LIGHTING = 0x00,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED = 0xff,
};

/** GenericDeviceType of the lighting class (ZCL Table 3-10). */
enum zzb_zcl_basic_generic_device_type {
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_INCANDESCENT = 0x00,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_SPOTLIGHT_HALOGEN = 0x01,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_HALOGEN_BULB = 0x02,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_CFL = 0x03,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_LINEAR_FLUORESCENT = 0x04,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_LED_BULB = 0x05,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_SPOTLIGHT_LED = 0x06,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_LED_STRIP = 0x07,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_LED_TUBE = 0x08,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_INDOOR_LUMINAIRE = 0x09,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_OUTDOOR_LUMINAIRE = 0x0a,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_PENDANT_LUMINAIRE = 0x0b,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_FLOOR_STANDING_LUMINAIRE = 0x0c,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_CONTROLLER = 0xe0,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_WALL_SWITCH = 0xe1,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_PORTABLE_REMOTE_CONTROLLER = 0xe2,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_MOTION_LIGHT_SENSOR = 0xe3,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_ACTUATOR = 0xf0,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_WALL_SOCKET = 0xf1,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_GATEWAY = 0xf2,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_PLUG_IN_UNIT = 0xf3,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_RETROFIT_ACTUATOR = 0xf4,
	ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED = 0xff,
};

/** CodeId of ProductCode (ZCL Table 3-11). */
enum zzb_zcl_basic_product_code_id {
	ZZB_ZCL_BASIC_PRODUCT_CODE_MANUFACTURER = 0x00,
	ZZB_ZCL_BASIC_PRODUCT_CODE_EAN = 0x01,
	ZZB_ZCL_BASIC_PRODUCT_CODE_GTIN = 0x02,
	ZZB_ZCL_BASIC_PRODUCT_CODE_UPC = 0x03,
	ZZB_ZCL_BASIC_PRODUCT_CODE_SKU = 0x04,
};

/**
 * Values of a Basic server (ZCL 3.2.2.2), all read-only and set by the application. ZCLVersion is
 * 8. GenericDeviceClass and GenericDeviceType default to 0xff in ZCL, while 0 means lighting and
 * incandescent, so the application sets them.
 */
struct zzb_zcl_basic {
	uint8_t app_version;
	uint8_t stack_version;
	uint8_t hw_version;
	const char *manufacturer_name;
	const char *model_id;
	const char *date_code;
	/** enum zzb_zcl_basic_power_source and ZZB_ZCL_BASIC_POWER_SOURCE_BATTERY_BACKUP. */
	uint8_t power_source;
	/** enum zzb_zcl_basic_generic_device_class. */
	uint8_t generic_device_class;
	/** enum zzb_zcl_basic_generic_device_type. */
	uint8_t generic_device_type;
	/**
	 * The length octet, the CodeId (enum zzb_zcl_basic_product_code_id) and the code
	 * (ZCL 3.2.2.2.11).
	 */
	const uint8_t *product_code;
	const char *product_url;
	const char *sw_build_id;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_basic_def;

/** A Basic server with the values v, a pointer to struct zzb_zcl_basic. */
#define ZZB_ZCL_BASIC_SERVER(v)                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_BASIC,                                                               \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_basic *)NULL),                                \
		.def = &zzb_zcl_basic_def,                                                         \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_BASIC_H_ */
