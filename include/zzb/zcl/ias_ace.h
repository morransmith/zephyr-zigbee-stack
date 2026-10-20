/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_
#define ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_

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

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IAS_ACE_H_ */
