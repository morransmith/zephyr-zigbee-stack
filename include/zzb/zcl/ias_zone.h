/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief IAS Zone cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_
#define ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IAS Zone cluster (ZCL 8.2). */
#define ZZB_ZCL_IAS_ZONE 0x0500

/** Attributes (ZCL Tables 8-3 and 8-8). */
#define ZZB_ZCL_IAS_ZONE_STATE                                  0x0000
#define ZZB_ZCL_IAS_ZONE_TYPE                                   0x0001
#define ZZB_ZCL_IAS_ZONE_STATUS                                 0x0002
#define ZZB_ZCL_IAS_ZONE_CIE_ADDRESS                            0x0010
#define ZZB_ZCL_IAS_ZONE_ID                                     0x0011
#define ZZB_ZCL_IAS_ZONE_NUMBER_OF_SENSITIVITY_LEVELS_SUPPORTED 0x0012
#define ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL              0x0013

/** ZoneState (ZCL Table 8-4). */
enum zzb_zcl_ias_zone_state {
	ZZB_ZCL_IAS_ZONE_STATE_NOT_ENROLLED = 0x00,
	ZZB_ZCL_IAS_ZONE_STATE_ENROLLED = 0x01,
};

/** ZoneType (ZCL Table 8-5); 0x8000 to 0xfffe are manufacturer-specific. */
enum zzb_zcl_ias_zone_type {
	ZZB_ZCL_IAS_ZONE_TYPE_STANDARD_CIE = 0x0000,
	ZZB_ZCL_IAS_ZONE_TYPE_MOTION_SENSOR = 0x000d,
	ZZB_ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH = 0x0015,
	ZZB_ZCL_IAS_ZONE_TYPE_DOOR_WINDOW_HANDLE = 0x0016,
	ZZB_ZCL_IAS_ZONE_TYPE_FIRE_SENSOR = 0x0028,
	ZZB_ZCL_IAS_ZONE_TYPE_WATER_SENSOR = 0x002a,
	ZZB_ZCL_IAS_ZONE_TYPE_CO_SENSOR = 0x002b,
	ZZB_ZCL_IAS_ZONE_TYPE_PERSONAL_EMERGENCY_DEVICE = 0x002c,
	ZZB_ZCL_IAS_ZONE_TYPE_VIBRATION_MOVEMENT_SENSOR = 0x002d,
	ZZB_ZCL_IAS_ZONE_TYPE_REMOTE_CONTROL = 0x010f,
	ZZB_ZCL_IAS_ZONE_TYPE_KEY_FOB = 0x0115,
	ZZB_ZCL_IAS_ZONE_TYPE_KEYPAD = 0x021d,
	ZZB_ZCL_IAS_ZONE_TYPE_STANDARD_WARNING_DEVICE = 0x0225,
	ZZB_ZCL_IAS_ZONE_TYPE_GLASS_BREAK_SENSOR = 0x0226,
	ZZB_ZCL_IAS_ZONE_TYPE_SECURITY_REPEATER = 0x0229,
	ZZB_ZCL_IAS_ZONE_TYPE_INVALID = 0xffff,
};

/** Bits of ZoneStatus (ZCL Table 8-6). */
#define ZZB_ZCL_IAS_ZONE_STATUS_ALARM1         BIT(0)
#define ZZB_ZCL_IAS_ZONE_STATUS_ALARM2         BIT(1)
#define ZZB_ZCL_IAS_ZONE_STATUS_TAMPER         BIT(2)
#define ZZB_ZCL_IAS_ZONE_STATUS_BATTERY        BIT(3)
#define ZZB_ZCL_IAS_ZONE_STATUS_SUPERVISION    BIT(4)
#define ZZB_ZCL_IAS_ZONE_STATUS_RESTORE        BIT(5)
#define ZZB_ZCL_IAS_ZONE_STATUS_TROUBLE        BIT(6)
#define ZZB_ZCL_IAS_ZONE_STATUS_AC             BIT(7)
#define ZZB_ZCL_IAS_ZONE_STATUS_TEST           BIT(8)
#define ZZB_ZCL_IAS_ZONE_STATUS_BATTERY_DEFECT BIT(9)

/** ZoneID, and the Zone ID of a command, when none is programmed (ZCL 8.2.2.3.1.4). */
#define ZZB_ZCL_IAS_ZONE_ID_NONE 0xff

/** Commands that the server receives (ZCL Table 8-9). */
#define ZZB_ZCL_IAS_ZONE_ENROLL_RSP           0x00
#define ZZB_ZCL_IAS_ZONE_INITIATE_NORMAL_MODE 0x01
#define ZZB_ZCL_IAS_ZONE_INITIATE_TEST_MODE   0x02

/** Enroll response code of Zone Enroll Response (ZCL Table 8-10). */
enum zzb_zcl_ias_zone_enroll_rsp_code {
	ZZB_ZCL_IAS_ZONE_ENROLL_SUCCESS = 0x00,
	ZZB_ZCL_IAS_ZONE_ENROLL_NOT_SUPPORTED = 0x01,
	ZZB_ZCL_IAS_ZONE_ENROLL_NO_ENROLL_PERMIT = 0x02,
	ZZB_ZCL_IAS_ZONE_ENROLL_TOO_MANY_ZONES = 0x03,
};

/** Commands that the server generates (ZCL Table 8-11). */
#define ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION 0x00
#define ZZB_ZCL_IAS_ZONE_ENROLL_REQ                 0x01

/**
 * Values of an IAS Zone server (ZCL 8.2.2.1). The application fills zone_type and
 * number_of_zone_sensitivity_levels_supported before zzb_node_init() and changes zone_status with
 * zzb_zcl_attr_set(), which sends the change to the CIE; these are read-only. The stack keeps the
 * enrollment and current_zone_sensitivity_level, which is writable, and saves them: the CIE writes
 * ias_cie_address and enrolls the zone with Zone Enroll Response. The CIE starts the test mode with
 * Initiate Test Mode; the stack keeps bit 8 (Test) of zone_status, and the application learns the
 * mode from zcl_cb of ZoneStatus.
 */
struct zzb_zcl_ias_zone {
	/** enum zzb_zcl_ias_zone_state. */
	uint8_t zone_state;
	/** enum zzb_zcl_ias_zone_type. */
	uint16_t zone_type;
	/** ZZB_ZCL_IAS_ZONE_STATUS_* bits. */
	uint16_t zone_status;
	/** IAS_CIE_Address: the IEEE address of the CIE; 0 and all ones mean none. */
	uint64_t ias_cie_address;
	/**
	 * ZoneID: the index of the zone in the zone table of the CIE, ZZB_ZCL_IAS_ZONE_ID_NONE
	 * before enrollment.
	 */
	uint8_t zone_id;
	/** 2 to 0xff; 0 takes the ZCL default 2 in zzb_node_init(). */
	uint8_t number_of_zone_sensitivity_levels_supported;
	/** 0, the default of the device, to number_of_zone_sensitivity_levels_supported. */
	uint8_t current_zone_sensitivity_level;
	/** Internal: the endpoint of the CIE, from which it wrote ias_cie_address. */
	uint8_t cie_ep;
	/** Internal: k_uptime_get() at the first change of zone_status not sent. */
	int64_t since;
	/** Internal: k_uptime_get() at which a refused notification is tried again, or 0. */
	int64_t retry;
	/** Internal: k_uptime_get() at the end of the test mode, 0 outside it. */
	int64_t test_end;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_client_def;

/** An IAS Zone server with the values v, a pointer to struct zzb_zcl_ias_zone. */
#define ZZB_ZCL_IAS_ZONE_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ZONE,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_ias_zone *)NULL),                             \
		.def = &zzb_zcl_ias_zone_def,                                                      \
	}

/**
 * An IAS Zone client, which the IAS CIE has (DTL). Zone Status Change Notification and Zone Enroll
 * Request go to zcl_cmd_cb; the application answers Zone Enroll Request with Zone Enroll Response
 * through zzb_zcl_cmd_respond().
 */
#define ZZB_ZCL_IAS_ZONE_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ZONE,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_ias_zone_client_def,                                               \
	}

/**
 * @brief Answer Zone Enroll Request within zcl_cmd_cb with Zone Enroll Response (ZCL 8.2.2.2.1).
 *
 * zone_id is the index of the zone in the zone table of the CIE; it counts only with
 * ZZB_ZCL_IAS_ZONE_ENROLL_SUCCESS.
 *
 * @return the result of zzb_zcl_cmd_respond().
 */
static inline int zzb_zcl_ias_zone_enroll_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
					      enum zzb_zcl_ias_zone_enroll_rsp_code code,
					      uint8_t zone_id)
{
	const uint8_t payload[] = {code, zone_id};

	return zzb_zcl_cmd_respond(node, cmd, ZZB_ZCL_IAS_ZONE_ENROLL_RSP, payload,
				   sizeof(payload));
}

/**
 * Fields of Zone Status Change Notification (ZCL 8.2.2.3.1) without Extended Status, which is
 * reserved.
 */
struct zzb_zcl_ias_zone_status_change_notification {
	/** ZZB_ZCL_IAS_ZONE_STATUS_* bits. */
	uint16_t zone_status;
	uint8_t zone_id;
	/** Quarter seconds from the change of ZoneStatus to the notification. */
	uint16_t delay;
};

/**
 * @brief Read the fields of Zone Status Change Notification that zcl_cmd_cb of the IAS Zone client
 * receives.
 */
static inline void zzb_zcl_ias_zone_status_change_notification_parse(
	const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ias_zone_status_change_notification *fields)
{
	fields->zone_status = sys_get_le16(&cmd->payload[0]);
	fields->zone_id = cmd->payload[3];
	fields->delay = sys_get_le16(&cmd->payload[4]);
}

/** Fields of Zone Enroll Request (ZCL 8.2.2.3.2). */
struct zzb_zcl_ias_zone_enroll_req {
	enum zzb_zcl_ias_zone_type zone_type;
	uint16_t manufacturer_code;
};

/**
 * @brief Read the fields of Zone Enroll Request that zcl_cmd_cb of the IAS Zone client receives.
 */
static inline void zzb_zcl_ias_zone_enroll_req_parse(const struct zzb_zcl_cmd *cmd,
						     struct zzb_zcl_ias_zone_enroll_req *fields)
{
	fields->zone_type = sys_get_le16(&cmd->payload[0]);
	fields->manufacturer_code = sys_get_le16(&cmd->payload[2]);
}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_ */
