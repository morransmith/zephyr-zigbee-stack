/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_BASIC_H_
#define ZZB_INCLUDE_ZZB_ZCL_BASIC_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Basic cluster (ZCL 3.2). */
#define ZZB_ZCL_BASIC 0x0000

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
	/** ZCL Table 3-8. */
	uint8_t power_source;
	uint8_t generic_device_class;
	uint8_t generic_device_type;
	/** The length octet, the CodeId and the code (ZCL 3.2.2.2.11). */
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
