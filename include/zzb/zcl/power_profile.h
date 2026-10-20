/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_
#define ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power Profile cluster (ZCL 3.17). */
#define ZZB_ZCL_POWER_PROFILE 0x001a

/** Attributes (ZCL Table 3-135). */
#define ZZB_ZCL_POWER_PROFILE_TOTAL_PROFILE_NUM   0x0000
#define ZZB_ZCL_POWER_PROFILE_MULTIPLE_SCHEDULING 0x0001
#define ZZB_ZCL_POWER_PROFILE_ENERGY_FORMATTING   0x0002
#define ZZB_ZCL_POWER_PROFILE_ENERGY_REMOTE       0x0003
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE       0x0004

/** Commands that the server receives and a client sends (ZCL Table 3-138). */
#define ZZB_ZCL_POWER_PROFILE_REQUEST                              0x00
#define ZZB_ZCL_POWER_PROFILE_STATE_REQUEST                        0x01
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE_RSP                        0x02
#define ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE_RSP       0x03
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_NOTIFICATION  0x04
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_RSP           0x05
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_REQUEST         0x06
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_REQUEST 0x07
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED_RSP               0x08

/** Commands that the server generates and a client receives (ZCL Table 3-139). */
#define ZZB_ZCL_POWER_PROFILE_NOTIFICATION                              0x00
#define ZZB_ZCL_POWER_PROFILE_RSP                                       0x01
#define ZZB_ZCL_POWER_PROFILE_STATE_RSP                                 0x02
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE                                 0x03
#define ZZB_ZCL_POWER_PROFILE_STATE_NOTIFICATION                        0x04
#define ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE                0x05
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_REQUEST            0x06
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_RSP          0x07
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_NOTIFICATION 0x08
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_NOTIFICATION         0x09
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_RSP                  0x0a
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED                        0x0b

/** Bits of ScheduleMode (ZCL Table 3-137). */
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CHEAPEST BIT(0)
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_GREENEST BIT(1)

/** Bits of Options of GetPowerProfilePriceExtended (ZCL Table 3-141). */
#define ZZB_ZCL_POWER_PROFILE_START_TIME_PRESENT BIT(0)
#define ZZB_ZCL_POWER_PROFILE_PRICE_AS_SCHEDULED BIT(1)

/** PowerProfileState of a Power Profile record (ZCL Table 3-140). */
enum zzb_zcl_power_profile_state {
	ZZB_ZCL_POWER_PROFILE_IDLE = 0x00,
	ZZB_ZCL_POWER_PROFILE_PROGRAMMED = 0x01,
	ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_RUNNING = 0x03,
	ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_PAUSED = 0x04,
	ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_WAITING_TO_START = 0x05,
	ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_WAITING_PAUSED = 0x06,
	ZZB_ZCL_POWER_PROFILE_ENDED = 0x07,
};

/**
 * Values of a Power Profile server (ZCL 3.17.4). The application fills the first four fields
 * before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only.
 * ScheduleMode is writable, saved and reportable.
 */
struct zzb_zcl_power_profile {
	/** The number of power profiles, 1 to 0xfe. */
	uint8_t total_profile_num;
	bool multiple_scheduling;
	/**
	 * The format of the Energy fields: bits 0 to 2 digits right of the decimal point, bits 3 to
	 * 6 digits left of it, bit 7 suppresses leading zeros (ZCL 3.17.4.3).
	 */
	uint8_t energy_formatting;
	/** Remote energy management is enabled. */
	bool energy_remote;
	/** ZZB_ZCL_POWER_PROFILE_SCHEDULE_CHEAPEST and _GREENEST; default 0. */
	uint8_t schedule_mode;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_power_profile_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_power_profile_client_def;

/**
 * A Power Profile server with the values v, a pointer to struct zzb_zcl_power_profile, which the
 * White Goods device has (DTL). Its commands go to zcl_cmd_cb after the length checks. The
 * application keeps the power profiles, energy phases, states and schedules, answers the commands
 * with zzb_zcl_cmd_respond() and sends the notifications and requests of the server with
 * zzb_zcl_send_bound().
 */
#define ZZB_ZCL_POWER_PROFILE_SERVER(v)                                                            \
	{                                                                                          \
		.id = ZZB_ZCL_POWER_PROFILE,                                                       \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_power_profile *)NULL),                        \
		.def = &zzb_zcl_power_profile_def,                                                 \
	}

/**
 * A Power Profile client, which the Home Gateway has (DTL). The commands of the server go to
 * zcl_cmd_cb after the length checks; the client sends its commands with zzb_zcl_send_bound().
 */
#define ZZB_ZCL_POWER_PROFILE_CLIENT()                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_POWER_PROFILE,                                                       \
		.server = false,                                                                   \
		.def = &zzb_zcl_power_profile_client_def,                                          \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_ */
