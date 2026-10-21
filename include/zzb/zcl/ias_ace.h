/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief IAS ACE cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_
#define ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_

#include <errno.h>
#include <string.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IAS ACE cluster (ZCL 8.3). */
#define ZZB_ZCL_IAS_ACE 0x0501

/** Commands that the server receives and a client sends (ZCL Table 8-13). */
#define ZZB_ZCL_IAS_ACE_ARM                    0x00
#define ZZB_ZCL_IAS_ACE_BYPASS                 0x01
#define ZZB_ZCL_IAS_ACE_EMERGENCY              0x02
#define ZZB_ZCL_IAS_ACE_FIRE                   0x03
#define ZZB_ZCL_IAS_ACE_PANIC                  0x04
#define ZZB_ZCL_IAS_ACE_GET_ZONE_ID_MAP        0x05
#define ZZB_ZCL_IAS_ACE_GET_ZONE_INFORMATION   0x06
#define ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS       0x07
#define ZZB_ZCL_IAS_ACE_GET_BYPASSED_ZONE_LIST 0x08
#define ZZB_ZCL_IAS_ACE_GET_ZONE_STATUS        0x09

/** Commands that the server generates and a client receives (ZCL Table 8-15). */
#define ZZB_ZCL_IAS_ACE_ARM_RSP                  0x00
#define ZZB_ZCL_IAS_ACE_GET_ZONE_ID_MAP_RSP      0x01
#define ZZB_ZCL_IAS_ACE_GET_ZONE_INFORMATION_RSP 0x02
#define ZZB_ZCL_IAS_ACE_ZONE_STATUS_CHANGED      0x03
#define ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED     0x04
#define ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS_RSP     0x05
#define ZZB_ZCL_IAS_ACE_SET_BYPASSED_ZONE_LIST   0x06
#define ZZB_ZCL_IAS_ACE_BYPASS_RSP               0x07
#define ZZB_ZCL_IAS_ACE_GET_ZONE_STATUS_RSP      0x08

/** Arm Mode of Arm (ZCL Table 8-14). */
enum zzb_zcl_ias_ace_arm_mode {
	ZZB_ZCL_IAS_ACE_MODE_DISARM = 0x00,
	ZZB_ZCL_IAS_ACE_MODE_DAY_HOME = 0x01,
	ZZB_ZCL_IAS_ACE_MODE_NIGHT_SLEEP = 0x02,
	ZZB_ZCL_IAS_ACE_MODE_ALL_ZONES = 0x03,
};

/** The longest Arm/Disarm Code that zzb_zcl_ias_ace_arm() sends, in octets (ZCL 8.3.2.3.1.3). */
#define ZZB_ZCL_IAS_ACE_CODE_MAX 8

/** Arm Notification of Arm Response (ZCL Table 8-16). */
enum zzb_zcl_ias_ace_arm_notification {
	ZZB_ZCL_IAS_ACE_ALL_ZONES_DISARMED = 0x00,
	ZZB_ZCL_IAS_ACE_DAY_HOME_ZONES_ARMED = 0x01,
	ZZB_ZCL_IAS_ACE_NIGHT_SLEEP_ZONES_ARMED = 0x02,
	ZZB_ZCL_IAS_ACE_ALL_ZONES_ARMED = 0x03,
	ZZB_ZCL_IAS_ACE_INVALID_CODE = 0x04,
	ZZB_ZCL_IAS_ACE_NOT_READY_TO_ARM = 0x05,
	ZZB_ZCL_IAS_ACE_ALREADY_DISARMED = 0x06,
};

/** Panel Status of Panel Status Changed and Get Panel Status Response (ZCL Table 8-17). */
enum zzb_zcl_ias_ace_panel_status {
	ZZB_ZCL_IAS_ACE_PANEL_DISARMED = 0x00,
	ZZB_ZCL_IAS_ACE_PANEL_ARMED_STAY = 0x01,
	ZZB_ZCL_IAS_ACE_PANEL_ARMED_NIGHT = 0x02,
	ZZB_ZCL_IAS_ACE_PANEL_ARMED_AWAY = 0x03,
	ZZB_ZCL_IAS_ACE_PANEL_EXIT_DELAY = 0x04,
	ZZB_ZCL_IAS_ACE_PANEL_ENTRY_DELAY = 0x05,
	ZZB_ZCL_IAS_ACE_PANEL_NOT_READY_TO_ARM = 0x06,
	ZZB_ZCL_IAS_ACE_PANEL_IN_ALARM = 0x07,
	ZZB_ZCL_IAS_ACE_PANEL_ARMING_STAY = 0x08,
	ZZB_ZCL_IAS_ACE_PANEL_ARMING_NIGHT = 0x09,
	ZZB_ZCL_IAS_ACE_PANEL_ARMING_AWAY = 0x0a,
};

/** Audible Notification (ZCL Figure 8-14); 0x80 to 0xff are manufacturer-specific. */
enum zzb_zcl_ias_ace_audible_notification {
	ZZB_ZCL_IAS_ACE_AUDIBLE_MUTE = 0x00,
	ZZB_ZCL_IAS_ACE_AUDIBLE_DEFAULT = 0x01,
};

/** Alarm Status of Panel Status Changed and Get Panel Status Response (ZCL Figure 8-16). */
enum zzb_zcl_ias_ace_alarm_status {
	ZZB_ZCL_IAS_ACE_ALARM_NONE = 0x00,
	ZZB_ZCL_IAS_ACE_ALARM_BURGLAR = 0x01,
	ZZB_ZCL_IAS_ACE_ALARM_FIRE = 0x02,
	ZZB_ZCL_IAS_ACE_ALARM_EMERGENCY = 0x03,
	ZZB_ZCL_IAS_ACE_ALARM_POLICE_PANIC = 0x04,
	ZZB_ZCL_IAS_ACE_ALARM_FIRE_PANIC = 0x05,
	ZZB_ZCL_IAS_ACE_ALARM_EMERGENCY_PANIC = 0x06,
};

/** Bypass Result of Bypass Response (ZCL Table 8-18). */
enum zzb_zcl_ias_ace_bypass_result {
	ZZB_ZCL_IAS_ACE_BYPASS_ZONE_BYPASSED = 0x00,
	ZZB_ZCL_IAS_ACE_BYPASS_ZONE_NOT_BYPASSED = 0x01,
	ZZB_ZCL_IAS_ACE_BYPASS_NOT_ALLOWED = 0x02,
	ZZB_ZCL_IAS_ACE_BYPASS_INVALID_ZONE_ID = 0x03,
	ZZB_ZCL_IAS_ACE_BYPASS_UNKNOWN_ZONE_ID = 0x04,
	ZZB_ZCL_IAS_ACE_BYPASS_INVALID_CODE = 0x05,
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ias_ace_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_ias_ace_client_def;

/**
 * An IAS ACE server, which the IAS CIE has (DTL). Its commands go to zcl_cmd_cb after the length
 * checks. The application keeps the zone table and the panel status, answers the commands with
 * zzb_zcl_cmd_respond() and sends Zone Status Changed, Panel Status Changed and Set Bypassed Zone
 * List unsolicited with zzb_zcl_send_bound().
 */
#define ZZB_ZCL_IAS_ACE_SERVER()                                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ACE,                                                             \
		.server = true,                                                                    \
		.def = &zzb_zcl_ias_ace_def,                                                       \
	}

/**
 * An IAS ACE client, which the IAS ACE device has (DTL). The commands of the server go to
 * zcl_cmd_cb after the length checks; the client sends its commands with zzb_zcl_send_bound().
 */
#define ZZB_ZCL_IAS_ACE_CLIENT()                                                                   \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ACE,                                                             \
		.server = false,                                                                   \
		.def = &zzb_zcl_ias_ace_client_def,                                                \
	}

/**
 * @brief Send Arm from the IAS ACE client on endpoint ep to the bound servers (ZCL 8.3.2.3.1).
 *
 * The code has len octets, NULL and 0 for none. zone_id is the index of the zone of the client in
 * the zone table of the CIE, ZZB_ZCL_IAS_ZONE_ID_NONE when none is programmed.
 *
 * @retval -EINVAL the code is longer than ZZB_ZCL_IAS_ACE_CODE_MAX.
 * @return the result of zzb_zcl_send_bound() otherwise.
 */
static inline int zzb_zcl_ias_ace_arm(struct zzb_node *node, uint8_t ep,
				      enum zzb_zcl_ias_ace_arm_mode arm_mode, const uint8_t *code,
				      size_t len, uint8_t zone_id)
{
	uint8_t payload[2 + ZZB_ZCL_IAS_ACE_CODE_MAX + 1] = {arm_mode, len};

	if (len > ZZB_ZCL_IAS_ACE_CODE_MAX) {
		return -EINVAL;
	}

	/* memcpy() needs a valid pointer also for 0 octets */
	if (len > 0) {
		memcpy(&payload[2], code, len);
	}

	payload[2 + len] = zone_id;
	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_IAS_ACE, ZZB_ZCL_IAS_ACE_ARM, payload, 3 + len);
}

/**
 * @brief Answer Arm within zcl_cmd_cb with Arm Response (ZCL 8.3.2.4.1).
 *
 * @return the result of zzb_zcl_cmd_respond().
 */
static inline int zzb_zcl_ias_ace_arm_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
					  enum zzb_zcl_ias_ace_arm_notification arm_notification)
{
	const uint8_t payload[] = {arm_notification};

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_IAS_ACE_ARM_RSP, payload, sizeof(payload));
}

/**
 * @brief Answer Get Panel Status within zcl_cmd_cb with Get Panel Status Response (ZCL
 * 8.3.2.4.6).
 *
 * seconds_remaining is the time left in the exit or entry delay in seconds, otherwise 0.
 *
 * @return the result of zzb_zcl_cmd_respond().
 */
static inline int
zzb_zcl_ias_ace_get_panel_status_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
				     enum zzb_zcl_ias_ace_panel_status panel_status,
				     uint8_t seconds_remaining,
				     enum zzb_zcl_ias_ace_audible_notification audible_notification,
				     enum zzb_zcl_ias_ace_alarm_status alarm_status)
{
	const uint8_t payload[] = {panel_status, seconds_remaining, audible_notification,
				   alarm_status};

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS_RSP, payload,
				   sizeof(payload));
}

/**
 * @brief Send Panel Status Changed from the IAS ACE server on endpoint ep to the bound clients (ZCL
 * 8.3.2.4.5).
 *
 * seconds_remaining is the time left in the exit or entry delay in seconds, otherwise 0.
 *
 * @return the result of zzb_zcl_send_bound().
 */
static inline int zzb_zcl_ias_ace_panel_status_changed(
	struct zzb_node *node, uint8_t ep, enum zzb_zcl_ias_ace_panel_status panel_status,
	uint8_t seconds_remaining, enum zzb_zcl_ias_ace_audible_notification audible_notification,
	enum zzb_zcl_ias_ace_alarm_status alarm_status)
{
	const uint8_t payload[] = {panel_status, seconds_remaining, audible_notification,
				   alarm_status};

	return zzb_zcl_send_bound(node, ep, ZZB_ZCL_IAS_ACE, ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED,
				  payload, sizeof(payload));
}

/** Fields of Arm (ZCL 8.3.2.3.1). */
struct zzb_zcl_ias_ace_arm {
	enum zzb_zcl_ias_ace_arm_mode arm_mode;
	/** The Arm/Disarm Code of code_len octets in the payload of the command. */
	const uint8_t *code;
	uint8_t code_len;
	uint8_t zone_id;
};

/** @brief Read the fields of Arm that zcl_cmd_cb of the IAS ACE server receives. */
static inline void zzb_zcl_ias_ace_arm_parse(const struct zzb_zcl_cmd *cmd,
					     struct zzb_zcl_ias_ace_arm *fields)
{
	fields->arm_mode = cmd->payload[0];
	fields->code_len = cmd->payload[1];
	fields->code = &cmd->payload[2];
	fields->zone_id = cmd->payload[2 + fields->code_len];
}

/** Fields of Arm Response (ZCL 8.3.2.4.1). */
struct zzb_zcl_ias_ace_arm_rsp {
	enum zzb_zcl_ias_ace_arm_notification arm_notification;
};

/** @brief Read the fields of Arm Response that zcl_cmd_cb of the IAS ACE client receives. */
static inline void zzb_zcl_ias_ace_arm_rsp_parse(const struct zzb_zcl_cmd *cmd,
						 struct zzb_zcl_ias_ace_arm_rsp *fields)
{
	fields->arm_notification = cmd->payload[0];
}

/**
 * Fields of Panel Status Changed (ZCL 8.3.2.4.5), which Get Panel Status Response also has (ZCL
 * 8.3.2.4.6).
 */
struct zzb_zcl_ias_ace_panel_status_changed {
	enum zzb_zcl_ias_ace_panel_status panel_status;
	/** Seconds. */
	uint8_t seconds_remaining;
	enum zzb_zcl_ias_ace_audible_notification audible_notification;
	enum zzb_zcl_ias_ace_alarm_status alarm_status;
};

/**
 * @brief Read the fields of Panel Status Changed, or of Get Panel Status Response, that zcl_cmd_cb
 * of the IAS ACE client receives.
 */
static inline void
zzb_zcl_ias_ace_panel_status_changed_parse(const struct zzb_zcl_cmd *cmd,
					   struct zzb_zcl_ias_ace_panel_status_changed *fields)
{
	fields->panel_status = cmd->payload[0];
	fields->seconds_remaining = cmd->payload[1];
	fields->audible_notification = cmd->payload[2];
	fields->alarm_status = cmd->payload[3];
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_ */
