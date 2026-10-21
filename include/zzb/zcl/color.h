/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Color Control cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_COLOR_H_
#define ZZB_INCLUDE_ZZB_ZCL_COLOR_H_

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Color Control cluster (ZCL 5.2). */
#define ZZB_ZCL_COLOR 0x0300

/** Attributes (ZCL Tables 5.3, 5.10 and 5.11). */
#define ZZB_ZCL_COLOR_REMAINING_TIME       0x0002
#define ZZB_ZCL_COLOR_CURRENT_X            0x0003
#define ZZB_ZCL_COLOR_CURRENT_Y            0x0004
#define ZZB_ZCL_COLOR_TEMPERATURE          0x0007
#define ZZB_ZCL_COLOR_MODE                 0x0008
#define ZZB_ZCL_COLOR_OPTIONS              0x000f
#define ZZB_ZCL_COLOR_NUMBER_OF_PRIMARIES  0x0010
#define ZZB_ZCL_COLOR_PRIMARY1_X           0x0011
#define ZZB_ZCL_COLOR_PRIMARY1_Y           0x0012
#define ZZB_ZCL_COLOR_PRIMARY1_INTENSITY   0x0013
#define ZZB_ZCL_COLOR_PRIMARY2_X           0x0015
#define ZZB_ZCL_COLOR_PRIMARY2_Y           0x0016
#define ZZB_ZCL_COLOR_PRIMARY2_INTENSITY   0x0017
#define ZZB_ZCL_COLOR_PRIMARY3_X           0x0019
#define ZZB_ZCL_COLOR_PRIMARY3_Y           0x001a
#define ZZB_ZCL_COLOR_PRIMARY3_INTENSITY   0x001b
#define ZZB_ZCL_COLOR_PRIMARY4_X           0x0020
#define ZZB_ZCL_COLOR_PRIMARY4_Y           0x0021
#define ZZB_ZCL_COLOR_PRIMARY4_INTENSITY   0x0022
#define ZZB_ZCL_COLOR_PRIMARY5_X           0x0024
#define ZZB_ZCL_COLOR_PRIMARY5_Y           0x0025
#define ZZB_ZCL_COLOR_PRIMARY5_INTENSITY   0x0026
#define ZZB_ZCL_COLOR_PRIMARY6_X           0x0028
#define ZZB_ZCL_COLOR_PRIMARY6_Y           0x0029
#define ZZB_ZCL_COLOR_PRIMARY6_INTENSITY   0x002a
#define ZZB_ZCL_COLOR_ENHANCED_MODE        0x4001
#define ZZB_ZCL_COLOR_CAPABILITIES         0x400a
#define ZZB_ZCL_COLOR_TEMP_PHYSICAL_MIN    0x400b
#define ZZB_ZCL_COLOR_TEMP_PHYSICAL_MAX    0x400c
#define ZZB_ZCL_COLOR_COUPLE_TEMP_MIN      0x400d
#define ZZB_ZCL_COLOR_START_UP_TEMPERATURE 0x4010

/** ColorMode, 0x00 to 0x02, and EnhancedColorMode (ZCL Tables 5.5 and 5.7). */
enum zzb_zcl_color_mode {
	ZZB_ZCL_COLOR_MODE_HUE_SATURATION = 0x00,
	ZZB_ZCL_COLOR_MODE_XY = 0x01,
	ZZB_ZCL_COLOR_MODE_TEMPERATURE = 0x02,
	ZZB_ZCL_COLOR_MODE_ENHANCED_HUE_SATURATION = 0x03,
};

/** Bit of Options (ZCL Table 5.6). */
#define ZZB_ZCL_COLOR_OPTIONS_EXECUTE_IF_OFF BIT(0)

/** Bits of ColorCapabilities (ZCL Table 5.8); the server of the stack has XY and TEMPERATURE. */
#define ZZB_ZCL_COLOR_CAP_HUE_SATURATION BIT(0)
#define ZZB_ZCL_COLOR_CAP_ENHANCED_HUE   BIT(1)
#define ZZB_ZCL_COLOR_CAP_COLOR_LOOP     BIT(2)
#define ZZB_ZCL_COLOR_CAP_XY             BIT(3)
#define ZZB_ZCL_COLOR_CAP_TEMPERATURE    BIT(4)

/** The StartUpColorTemperatureMireds that keeps the value before the power-down (ZCL Table 5.9). */
#define ZZB_ZCL_COLOR_START_UP_TEMPERATURE_PREVIOUS 0xffff

/** An unknown NumberOfPrimaries (ZCL 5.2.2.2.2.1). */
#define ZZB_ZCL_COLOR_NUMBER_OF_PRIMARIES_UNKNOWN 0xff

/** Special values of the intensity of a primary (ZCL 5.2.2.2.2.4). */
#define ZZB_ZCL_COLOR_PRIMARY_INTENSITY_HIGHEST     0xfe
#define ZZB_ZCL_COLOR_PRIMARY_INTENSITY_UNAVAILABLE 0xff

/** Commands that the server receives and a client sends (ZCL Table 5.13). */
#define ZZB_ZCL_COLOR_MOVE_TO_HUE                     0x00
#define ZZB_ZCL_COLOR_MOVE_HUE                        0x01
#define ZZB_ZCL_COLOR_STEP_HUE                        0x02
#define ZZB_ZCL_COLOR_MOVE_TO_SATURATION              0x03
#define ZZB_ZCL_COLOR_MOVE_SATURATION                 0x04
#define ZZB_ZCL_COLOR_STEP_SATURATION                 0x05
#define ZZB_ZCL_COLOR_MOVE_TO_HUE_SATURATION          0x06
#define ZZB_ZCL_COLOR_MOVE_TO_COLOR                   0x07
#define ZZB_ZCL_COLOR_MOVE_COLOR                      0x08
#define ZZB_ZCL_COLOR_STEP_COLOR                      0x09
#define ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE             0x0a
#define ZZB_ZCL_COLOR_ENHANCED_MOVE_TO_HUE            0x40
#define ZZB_ZCL_COLOR_ENHANCED_MOVE_HUE               0x41
#define ZZB_ZCL_COLOR_ENHANCED_STEP_HUE               0x42
#define ZZB_ZCL_COLOR_ENHANCED_MOVE_TO_HUE_SATURATION 0x43
#define ZZB_ZCL_COLOR_LOOP_SET                        0x44
#define ZZB_ZCL_COLOR_STOP_MOVE_STEP                  0x47
#define ZZB_ZCL_COLOR_MOVE_TEMPERATURE                0x4b
#define ZZB_ZCL_COLOR_STEP_TEMPERATURE                0x4c

/** A primary of the light (ZCL 5.2.2.2.2). */
struct zzb_zcl_color_primary {
	/** x and y as in CurrentX and CurrentY. */
	uint16_t x;
	uint16_t y;
	/**
	 * ZZB_ZCL_COLOR_PRIMARY_INTENSITY_HIGHEST for the primary of the highest intensity,
	 * ZZB_ZCL_COLOR_PRIMARY_INTENSITY_UNAVAILABLE when it is not available.
	 */
	uint8_t intensity;
};

/**
 * Values of a Color Control server (ZCL 5.2.2.2) with the XY and color temperature capabilities.
 * The application fills the fields up to primaries before zzb_node_init() and changes them with
 * zzb_zcl_attr_set(); they are read-only. ColorTemperatureMireds, the modes and the writable
 * attributes are saved, CurrentX and CurrentY in XY mode; in color temperature mode with the XY
 * capability they follow ColorTemperatureMireds.
 */
struct zzb_zcl_color {
	/** ZZB_ZCL_COLOR_CAP_XY, ZZB_ZCL_COLOR_CAP_TEMPERATURE or both. */
	uint16_t color_capabilities;
	/** The range of ColorTemperatureMireds of the light; ZCL defaults 0 and 0xfeff. */
	uint16_t color_temp_physical_min;
	uint16_t color_temp_physical_max;
	/** ColorTemperatureMireds that the highest level gives (ZCL 5.2.2.1.1). */
	uint16_t couple_color_temp_min;
	/** 0 to 6, or ZZB_ZCL_COLOR_NUMBER_OF_PRIMARIES_UNKNOWN. */
	uint8_t number_of_primaries;
	struct zzb_zcl_color_primary primaries[6];
	/** Tenths of a second. */
	uint16_t remaining_time;
	/** x and y of CIE 1931 times 65536, 0 to 0xfeff (ZCL 5.2.2.2.1.4). */
	uint16_t current_x;
	uint16_t current_y;
	/** Mireds, 1000000 / kelvins. */
	uint16_t color_temperature;
	/** The attributes that set the color: ZZB_ZCL_COLOR_MODE_XY or _TEMPERATURE. */
	uint8_t color_mode;
	/** enum zzb_zcl_color_mode, equal to color_mode. */
	uint8_t enhanced_color_mode;
	/** ZZB_ZCL_COLOR_OPTIONS_* bits. */
	uint8_t options;
	/** Mireds or ZZB_ZCL_COLOR_START_UP_TEMPERATURE_PREVIOUS. */
	uint16_t start_up_color_temperature;
	/**
	 * Factory value of StartUpColorTemperatureMireds, set by the application. A node without a
	 * record and the reset of the node take it.
	 */
	uint16_t start_up_default;
	/** Internal: the transition of CurrentX and CurrentY or of ColorTemperatureMireds. */
	struct zzb_zcl_transition transition;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_color_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_color_client_def;

/** A Color Control server with the values v, a pointer to struct zzb_zcl_color. */
#define ZZB_ZCL_COLOR_SERVER(v)                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_COLOR,                                                               \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_color *)NULL),                                \
		.def = &zzb_zcl_color_def,                                                         \
	}

/** A Color Control client, which color controllers have (DTL). */
#define ZZB_ZCL_COLOR_CLIENT()                                                                     \
	{                                                                                          \
		.id = ZZB_ZCL_COLOR,                                                               \
		.server = false,                                                                   \
		.def = &zzb_zcl_color_client_def,                                                  \
	}

/**
 * @brief Send Move to Color Temperature to the bound destinations (ZCL 5.2.2.3.14).
 *
 * ColorTemperatureMireds moves to mireds over transition_time tenths of a second. The optional
 * OptionsMask and OptionsOverride are left out.
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_color_move_to_temperature(struct zzb_node *node, uint8_t ep,
						    uint16_t mireds, uint16_t transition_time)
{
	uint8_t payload[4];

	sys_put_le16(mireds, payload);
	sys_put_le16(transition_time, payload + 2);
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_COLOR, ZZB_ZCL_COLOR_MOVE_TO_TEMPERATURE,
				  payload, sizeof(payload));
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_COLOR_H_ */
