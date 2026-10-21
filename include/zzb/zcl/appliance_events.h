/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_EVENTS_H_
#define ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_EVENTS_H_

#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EN50523 Appliance Events and Alerts cluster (ZCL 15.4). */
#define ZZB_ZCL_APPLIANCE_EVENTS 0x0b02

/** Command that the server receives and a client sends (ZCL Table 15-17). */
#define ZZB_ZCL_APPLIANCE_EVENTS_GET_ALERTS 0x00

/** Commands that the server generates and a client receives (ZCL Table 15-18). */
#define ZZB_ZCL_APPLIANCE_EVENTS_GET_ALERTS_RSP      0x00
#define ZZB_ZCL_APPLIANCE_EVENTS_ALERTS_NOTIFICATION 0x01
#define ZZB_ZCL_APPLIANCE_EVENTS_EVENT_NOTIFICATION  0x02

/** Fields of Alerts Count (ZCL Table 15-19). */
#define ZZB_ZCL_APPLIANCE_ALERTS_COUNT GENMASK(3, 0)
#define ZZB_ZCL_APPLIANCE_ALERTS_TYPE  GENMASK(7, 4)

/** ZZB_ZCL_APPLIANCE_ALERTS_TYPE (ZCL Table 15-19). */
enum zzb_zcl_appliance_alerts_type {
	ZZB_ZCL_APPLIANCE_ALERTS_UNSTRUCTURED = 0x0,
};

/** Fields of an alert structure (ZCL Table 15-20). */
#define ZZB_ZCL_APPLIANCE_ALERT_ID                GENMASK(7, 0)
#define ZZB_ZCL_APPLIANCE_ALERT_CATEGORY          GENMASK(11, 8)
#define ZZB_ZCL_APPLIANCE_ALERT_PRESENCE_RECOVERY GENMASK(13, 12)
#define ZZB_ZCL_APPLIANCE_ALERT_MANUFACTURER      GENMASK(23, 16)

/** ZZB_ZCL_APPLIANCE_ALERT_CATEGORY (ZCL Table 15-20). */
enum zzb_zcl_appliance_alert_category {
	ZZB_ZCL_APPLIANCE_ALERT_WARNING = 0x1,
	ZZB_ZCL_APPLIANCE_ALERT_DANGER = 0x2,
	ZZB_ZCL_APPLIANCE_ALERT_FAILURE = 0x3,
};

/** ZZB_ZCL_APPLIANCE_ALERT_PRESENCE_RECOVERY (ZCL Table 15-20). */
enum zzb_zcl_appliance_alert_presence {
	ZZB_ZCL_APPLIANCE_ALERT_RECOVERY = 0x0,
	ZZB_ZCL_APPLIANCE_ALERT_PRESENCE = 0x1,
};

/**
 * Event Identification of Event Notification (ZCL Table 15-21); 0x40 to 0xf6 and 0xf8 to 0xff are
 * manufacturer-specific.
 */
enum zzb_zcl_appliance_event {
	ZZB_ZCL_APPLIANCE_END_OF_CYCLE = 0x01,
	ZZB_ZCL_APPLIANCE_TEMPERATURE_REACHED = 0x04,
	ZZB_ZCL_APPLIANCE_END_OF_COOKING = 0x05,
	ZZB_ZCL_APPLIANCE_SWITCHING_OFF = 0x06,
	ZZB_ZCL_APPLIANCE_WRONG_DATA = 0xf7,
};

extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_events_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_events_client_def;

/**
 * An Appliance Events and Alerts server, which the White Goods device has (DTL); the cluster has no
 * attributes. Get Alerts goes to zcl_cmd_cb. The application keeps the alerts, answers Get Alerts
 * with zzb_zcl_cmd_respond() and sends Alerts Notification and Event Notification with
 * zzb_zcl_send_bound().
 */
#define ZZB_ZCL_APPLIANCE_EVENTS_SERVER()                                                          \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_EVENTS,                                                    \
		.server = true,                                                                    \
		.def = &zzb_zcl_appliance_events_def,                                              \
	}

/**
 * An Appliance Events and Alerts client. The commands of the server go to zcl_cmd_cb after the
 * length checks.
 */
#define ZZB_ZCL_APPLIANCE_EVENTS_CLIENT()                                                          \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_EVENTS,                                                    \
		.server = false,                                                                   \
		.def = &zzb_zcl_appliance_events_client_def,                                       \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_EVENTS_H_ */
