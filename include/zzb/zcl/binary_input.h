/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Binary Input (Basic) cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_BINARY_INPUT_H_
#define ZZB_INCLUDE_ZZB_ZCL_BINARY_INPUT_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Binary Input (Basic) cluster (ZCL 3.14.5). */
#define ZZB_ZCL_BINARY_INPUT 0x000f

/** Attributes (ZCL Table 3-81). @{ */
#define ZZB_ZCL_BINARY_INPUT_ACTIVE_TEXT      0x0004
#define ZZB_ZCL_BINARY_INPUT_DESCRIPTION      0x001c
#define ZZB_ZCL_BINARY_INPUT_INACTIVE_TEXT    0x002e
#define ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE   0x0051
#define ZZB_ZCL_BINARY_INPUT_POLARITY         0x0054
#define ZZB_ZCL_BINARY_INPUT_PRESENT_VALUE    0x0055
#define ZZB_ZCL_BINARY_INPUT_RELIABILITY      0x0067
#define ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS     0x006f
#define ZZB_ZCL_BINARY_INPUT_APPLICATION_TYPE 0x0100
/** @} */

/** Bits of StatusFlags (ZCL 3.14.11.3). @{ */
#define ZZB_ZCL_BINARY_INPUT_FLAG_IN_ALARM       BIT(0)
#define ZZB_ZCL_BINARY_INPUT_FLAG_FAULT          BIT(1)
#define ZZB_ZCL_BINARY_INPUT_FLAG_OVERRIDDEN     BIT(2)
#define ZZB_ZCL_BINARY_INPUT_FLAG_OUT_OF_SERVICE BIT(3)
/** @} */

/** Polarity (ZCL 3.14.11.16). */
enum zzb_zcl_binary_input_polarity {
	ZZB_ZCL_BINARY_INPUT_POLARITY_NORMAL = 0x00,
	ZZB_ZCL_BINARY_INPUT_POLARITY_REVERSE = 0x01,
};

/** Reliability (ZCL 3.14.11.9) without MULTI-STATE-FAULT, which is for multistate clusters. */
enum zzb_zcl_binary_input_reliability {
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_NO_FAULT_DETECTED = 0x00,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_NO_SENSOR = 0x01,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_OVER_RANGE = 0x02,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_UNDER_RANGE = 0x03,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_OPEN_LOOP = 0x04,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_SHORTED_LOOP = 0x05,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_NO_OUTPUT = 0x06,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_UNRELIABLE_OTHER = 0x07,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_PROCESS_ERROR = 0x08,
	ZZB_ZCL_BINARY_INPUT_RELIABILITY_CONFIGURATION_ERROR = 0x0a,
};

/**
 * Values of a Binary Input (Basic) server (ZCL 3.14.5.4.2). The application fills the fields other
 * than out_of_service before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only, except present_value, which a remote write sets while OutOfService is TRUE.
 * OutOfService is writable and saved. The stack keeps the OUT_OF_SERVICE bit of status_flags equal
 * to out_of_service and the FAULT bit set while reliability is not
 * ZZB_ZCL_BINARY_INPUT_RELIABILITY_NO_FAULT_DETECTED.
 */
struct zzb_zcl_binary_input {
	/** Up to 16 printable ASCII characters, or NULL. */
	const char *active_text;
	/** ZHA 2.3.1 names its binary sensor by it. */
	const char *description;
	/** Up to 16 printable ASCII characters, or NULL. */
	const char *inactive_text;
	/** The application stops setting present_value while it is TRUE. */
	bool out_of_service;
	/** enum zzb_zcl_binary_input_polarity; the application applies it to its input. */
	uint8_t polarity;
	/** The state of the input with polarity applied. */
	bool present_value;
	/** enum zzb_zcl_binary_input_reliability. */
	uint8_t reliability;
	/** ZZB_ZCL_BINARY_INPUT_FLAG_* bits. */
	uint8_t status_flags;
	/** Group, type and index (ZCL 3.14.11.19). */
	uint32_t application_type;
};

/** Internal: referenced by the instance macro of the cluster. */
extern const struct zzb_zcl_cluster_def zzb_zcl_binary_input_def;

/** A Binary Input (Basic) server with the values v, a pointer to struct zzb_zcl_binary_input. */
#define ZZB_ZCL_BINARY_INPUT_SERVER(v)                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_BINARY_INPUT,                                                        \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_binary_input *)NULL),                         \
		.def = &zzb_zcl_binary_input_def,                                                  \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_BINARY_INPUT_H_ */
