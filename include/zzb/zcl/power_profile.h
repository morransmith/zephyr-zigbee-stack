/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Power Profile cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_
#define ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power Profile cluster (ZCL 3.17). */
#define ZZB_ZCL_POWER_PROFILE 0x001a

/** Attributes (ZCL Table 3-135). @{ */
#define ZZB_ZCL_POWER_PROFILE_TOTAL_PROFILE_NUM   0x0000
#define ZZB_ZCL_POWER_PROFILE_MULTIPLE_SCHEDULING 0x0001
#define ZZB_ZCL_POWER_PROFILE_ENERGY_FORMATTING   0x0002
#define ZZB_ZCL_POWER_PROFILE_ENERGY_REMOTE       0x0003
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE       0x0004
/** @} */

/** Commands that the server receives and a client sends (ZCL Table 3-138). @{ */
#define ZZB_ZCL_POWER_PROFILE_REQUEST                              0x00
#define ZZB_ZCL_POWER_PROFILE_STATE_REQUEST                        0x01
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE_RSP                        0x02
#define ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE_RSP       0x03
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_NOTIFICATION  0x04
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_RSP           0x05
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_REQUEST         0x06
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_REQUEST 0x07
#define ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED_RSP               0x08
/** @} */

/** Commands that the server generates and a client receives (ZCL Table 3-139). @{ */
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
/** @} */

/** Bits of ScheduleMode (ZCL Table 3-137). @{ */
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_CHEAPEST BIT(0)
#define ZZB_ZCL_POWER_PROFILE_SCHEDULE_GREENEST BIT(1)
/** @} */

/** Bits of Options of GetPowerProfilePriceExtended (ZCL Table 3-141). @{ */
#define ZZB_ZCL_POWER_PROFILE_START_TIME_PRESENT BIT(0)
#define ZZB_ZCL_POWER_PROFILE_PRICE_AS_SCHEDULED BIT(1)
/** @} */

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

/** EnergyFormatting with digits left and right of the decimal point (ZCL 3.17.4.3). */
#define ZZB_ZCL_POWER_PROFILE_FORMATTING(left, right, suppress_zeros)                              \
	(FIELD_PREP(GENMASK(6, 3), (left)) | FIELD_PREP(GENMASK(2, 0), (right)) |                  \
	 ((suppress_zeros) ? BIT(7) : 0))

/** PowerProfileID of a request for every power profile (ZCL 3.17.5.1.1.1). */
#define ZZB_ZCL_POWER_PROFILE_ID_ALL 0x00

/** EnergyPhaseID of a power profile in the IDLE state (ZCL 3.17.6.1.1.1). */
#define ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_INVALID 0xff

/**
 * MaxActivationDelay of an energy phase that allows no pause before it, and of the first one.
 * @{
 */
#define ZZB_ZCL_POWER_PROFILE_ACTIVATION_DELAY_NONE  0x0000
#define ZZB_ZCL_POWER_PROFILE_ACTIVATION_DELAY_FIRST 0xffff
/** @} */

/**
 * Values of a Power Profile server (ZCL 3.17.4). The application fills the first four fields
 * before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only.
 * ScheduleMode is writable, saved and reportable.
 */
struct zzb_zcl_power_profile {
	/** The number of power profiles, 1 to 0xfe. */
	uint8_t total_profile_num;
	bool multiple_scheduling;
	/** The format of the Energy fields, ZZB_ZCL_POWER_PROFILE_FORMATTING(). */
	uint8_t energy_formatting;
	/** Remote energy management is enabled. */
	bool energy_remote;
	/** ZZB_ZCL_POWER_PROFILE_SCHEDULE_CHEAPEST and _GREENEST; default 0. */
	uint8_t schedule_mode;
};

/** Internal: referenced by the instance macros of the clusters. @{ */
extern const struct zzb_zcl_cluster_def zzb_zcl_power_profile_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_power_profile_client_def;
/** @} */

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

/** The most energy phases of a PowerProfileResponse in the ASDU of 82 octets. */
#define ZZB_ZCL_POWER_PROFILE_PHASES_MAX 7

/** The most records of a PowerProfileStateResponse in the ASDU of 82 octets. */
#define ZZB_ZCL_POWER_PROFILE_RECORDS_MAX 19

/** An energy phase of PowerProfileNotification and PowerProfileResponse (ZCL 3.17.6.1.1.1). */
struct zzb_zcl_power_profile_phase {
	uint8_t energy_phase_id;
	uint8_t macro_phase_id;
	/** Minutes. */
	uint16_t expected_duration;
	/** W. */
	uint16_t peak_power;
	/** Wh in the format of EnergyFormatting. */
	uint16_t energy;
	/** Minutes, ZZB_ZCL_POWER_PROFILE_ACTIVATION_DELAY_FIRST or _NONE. */
	uint16_t max_activation_delay;
};

/** A Power Profile record of PowerProfileStateResponse (ZCL 3.17.6.3.1.1). */
struct zzb_zcl_power_profile_record {
	uint8_t power_profile_id;
	/** ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_INVALID in the IDLE state. */
	uint8_t energy_phase_id;
	/** A client may schedule the power profile. */
	bool remote_control;
	enum zzb_zcl_power_profile_state state;
};

/** Fields of PowerProfileRequest (ZCL 3.17.5.1.1). */
struct zzb_zcl_power_profile_request {
	/** 1 to TotalProfileNum, or ZZB_ZCL_POWER_PROFILE_ID_ALL. */
	uint8_t power_profile_id;
};

/** Read the fields of PowerProfileRequest that zcl_cmd_cb receives. */
static inline void zzb_zcl_power_profile_request_parse(const struct zzb_zcl_cmd *cmd,
						       struct zzb_zcl_power_profile_request *fields)
{
	fields->power_profile_id = cmd->payload[0];
}

/**
 * @brief Answer PowerProfileRequest within zcl_cmd_cb with PowerProfileResponse (ZCL 3.17.6.2).
 *
 * Sends the count phases of the power profile power_profile_id of total_profile_num profiles.
 *
 * @return the result of zzb_zcl_cmd_respond(), or -EMSGSIZE for more than
 * ZZB_ZCL_POWER_PROFILE_PHASES_MAX phases.
 */
static inline int zzb_zcl_power_profile_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
					    uint8_t total_profile_num, uint8_t power_profile_id,
					    const struct zzb_zcl_power_profile_phase *phases,
					    size_t count)
{
	/* TotalProfileNum, PowerProfileID, Num of Transferred Phases and 10 octets per phase */
	uint8_t payload[3 + ZZB_ZCL_POWER_PROFILE_PHASES_MAX * 10];
	uint8_t *p = payload;

	if (count > ZZB_ZCL_POWER_PROFILE_PHASES_MAX) {
		return -EMSGSIZE;
	}

	*p++ = total_profile_num;
	*p++ = power_profile_id;
	*p++ = count;
	for (size_t i = 0; i < count; i++) {
		*p++ = phases[i].energy_phase_id;
		*p++ = phases[i].macro_phase_id;
		sys_put_le16(phases[i].expected_duration, p);
		sys_put_le16(phases[i].peak_power, p + 2);
		sys_put_le16(phases[i].energy, p + 4);
		sys_put_le16(phases[i].max_activation_delay, p + 6);
		p += 8;
	}

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_POWER_PROFILE_RSP, payload, p - payload);
}

/**
 * @brief Answer PowerProfileStateRequest within zcl_cmd_cb with PowerProfileStateResponse (ZCL
 * 3.17.6.3).
 *
 * @return the result of zzb_zcl_cmd_respond(), or -EMSGSIZE for more than
 * ZZB_ZCL_POWER_PROFILE_RECORDS_MAX records.
 */
static inline int
zzb_zcl_power_profile_state_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
				const struct zzb_zcl_power_profile_record *records, size_t count)
{
	/* Power Profile Count and 4 octets per record */
	uint8_t payload[1 + ZZB_ZCL_POWER_PROFILE_RECORDS_MAX * 4];
	uint8_t *p = payload;

	if (count > ZZB_ZCL_POWER_PROFILE_RECORDS_MAX) {
		return -EMSGSIZE;
	}

	*p++ = count;
	for (size_t i = 0; i < count; i++) {
		*p++ = records[i].power_profile_id;
		*p++ = records[i].energy_phase_id;
		*p++ = records[i].remote_control;
		*p++ = records[i].state;
	}

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_POWER_PROFILE_STATE_RSP, payload,
				   p - payload);
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_POWER_PROFILE_H_ */
