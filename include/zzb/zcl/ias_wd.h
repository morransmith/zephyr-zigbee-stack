/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IAS_WD_H_
#define ZZB_INCLUDE_ZZB_ZCL_IAS_WD_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IAS WD cluster (ZCL 8.4). */
#define ZZB_ZCL_IAS_WD 0x0502

/** Attribute (ZCL Table 8-19). */
#define ZZB_ZCL_IAS_WD_MAX_DURATION 0x0000

/** Commands that the server receives and a client sends (ZCL Table 8-20). */
#define ZZB_ZCL_IAS_WD_START_WARNING 0x00
#define ZZB_ZCL_IAS_WD_SQUAWK        0x01

/**
 * Fields of the first octet of Start Warning (ZCL Figure 8-21) for FIELD_GET(), at the positions
 * that zigpy 2.3.0 uses: warning mode, strobe, siren level.
 */
#define ZZB_ZCL_IAS_WD_WARNING_MODE GENMASK(7, 4)
#define ZZB_ZCL_IAS_WD_STROBE       GENMASK(3, 2)
#define ZZB_ZCL_IAS_WD_SIREN_LEVEL  GENMASK(1, 0)

/** Warning Mode of Start Warning (ZCL Table 8-21). */
enum zzb_zcl_ias_wd_warning_mode {
	ZZB_ZCL_IAS_WD_WARNING_MODE_STOP = 0,
	ZZB_ZCL_IAS_WD_WARNING_MODE_BURGLAR = 1,
	ZZB_ZCL_IAS_WD_WARNING_MODE_FIRE = 2,
	ZZB_ZCL_IAS_WD_WARNING_MODE_EMERGENCY = 3,
	ZZB_ZCL_IAS_WD_WARNING_MODE_POLICE_PANIC = 4,
	ZZB_ZCL_IAS_WD_WARNING_MODE_FIRE_PANIC = 5,
	ZZB_ZCL_IAS_WD_WARNING_MODE_EMERGENCY_PANIC = 6,
};

/** Strobe of Start Warning (ZCL Table 8-22). */
enum zzb_zcl_ias_wd_strobe {
	ZZB_ZCL_IAS_WD_STROBE_NONE = 0,
	ZZB_ZCL_IAS_WD_STROBE_PARALLEL = 1,
};

/** Siren Level of Start Warning (ZCL Table 8-23). */
enum zzb_zcl_ias_wd_siren_level {
	ZZB_ZCL_IAS_WD_SIREN_LEVEL_LOW = 0,
	ZZB_ZCL_IAS_WD_SIREN_LEVEL_MEDIUM = 1,
	ZZB_ZCL_IAS_WD_SIREN_LEVEL_HIGH = 2,
	ZZB_ZCL_IAS_WD_SIREN_LEVEL_VERY_HIGH = 3,
};

/** Strobe Level of Start Warning (ZCL Table 8-24). */
enum zzb_zcl_ias_wd_strobe_level {
	ZZB_ZCL_IAS_WD_STROBE_LEVEL_LOW = 0x00,
	ZZB_ZCL_IAS_WD_STROBE_LEVEL_MEDIUM = 0x01,
	ZZB_ZCL_IAS_WD_STROBE_LEVEL_HIGH = 0x02,
	ZZB_ZCL_IAS_WD_STROBE_LEVEL_VERY_HIGH = 0x03,
};

/** Fields of the octet of Squawk (ZCL Figure 8-22): squawk mode, strobe, squawk level. */
#define ZZB_ZCL_IAS_WD_SQUAWK_MODE   GENMASK(7, 4)
#define ZZB_ZCL_IAS_WD_SQUAWK_STROBE BIT(3)
#define ZZB_ZCL_IAS_WD_SQUAWK_LEVEL  GENMASK(1, 0)

/** Squawk Mode of Squawk (ZCL Table 8-25). */
enum zzb_zcl_ias_wd_squawk_mode {
	ZZB_ZCL_IAS_WD_SQUAWK_MODE_ARMED = 0,
	ZZB_ZCL_IAS_WD_SQUAWK_MODE_DISARMED = 1,
};

/** Squawk Level of Squawk (ZCL Table 8-27). */
enum zzb_zcl_ias_wd_squawk_level {
	ZZB_ZCL_IAS_WD_SQUAWK_LEVEL_LOW = 0,
	ZZB_ZCL_IAS_WD_SQUAWK_LEVEL_MEDIUM = 1,
	ZZB_ZCL_IAS_WD_SQUAWK_LEVEL_HIGH = 2,
	ZZB_ZCL_IAS_WD_SQUAWK_LEVEL_VERY_HIGH = 3,
};

/**
 * Values of an IAS WD server (ZCL 8.4.2.1). MaxDuration is writable and saved. Start Warning and
 * Squawk go to zcl_cmd_cb after the checks of the stack, and the application drives the siren and
 * the strobe. Start Warning comes as a copy with its 5 octets, the warning duration limited to the
 * whole seconds left of MaxDuration since the start of the continuous warning and the strobe duty
 * cycle rounded to the nearest multiple of 10, at most 100. A Start Warning other than Stop
 * (warning mode 0 without strobe) that zcl_cmd_cb accepts with a duration above 0 starts or
 * continues the warning; at its end the stack passes Start Warning with all fields 0. Squawk
 * during a warning does not reach zcl_cmd_cb.
 */
struct zzb_zcl_ias_wd {
	/** Seconds, 0x0000 to 0xfffe; default 240. */
	uint16_t max_duration;
	/** Internal: k_uptime_get() at the start of the continuous warning. */
	int64_t warning_start;
	/** Internal: k_uptime_get() at the end of the warning, 0 while no warning runs. */
	int64_t warning_end;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ias_wd_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_ias_wd_client_def;

/** An IAS WD server with the values v, a pointer to struct zzb_zcl_ias_wd. */
#define ZZB_ZCL_IAS_WD_SERVER(v)                                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_WD,                                                              \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_ias_wd *)NULL),                               \
		.def = &zzb_zcl_ias_wd_def,                                                        \
	}

/** An IAS WD client, which the IAS CIE has (DTL); the server generates no commands. */
#define ZZB_ZCL_IAS_WD_CLIENT()                                                                    \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_WD,                                                              \
		.server = false,                                                                   \
		.def = &zzb_zcl_ias_wd_client_def,                                                 \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IAS_WD_H_ */
