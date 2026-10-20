/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_DOOR_LOCK_H_
#define ZZB_INCLUDE_ZZB_ZCL_DOOR_LOCK_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Door Lock cluster (ZCL 7.3). */
#define ZZB_ZCL_DOOR_LOCK 0x0101

/** Attributes (ZCL Tables 7-8, 7-13, 7-14, 7-20 and 7-21). */
#define ZZB_ZCL_DOOR_LOCK_LOCK_STATE                       0x0000
#define ZZB_ZCL_DOOR_LOCK_LOCK_TYPE                        0x0001
#define ZZB_ZCL_DOOR_LOCK_ACTUATOR_ENABLED                 0x0002
#define ZZB_ZCL_DOOR_LOCK_DOOR_STATE                       0x0003
#define ZZB_ZCL_DOOR_LOCK_NUMBER_OF_TOTAL_USERS_SUPPORTED  0x0011
#define ZZB_ZCL_DOOR_LOCK_NUMBER_OF_PIN_USERS_SUPPORTED    0x0012
#define ZZB_ZCL_DOOR_LOCK_MAX_PIN_CODE_LENGTH              0x0017
#define ZZB_ZCL_DOOR_LOCK_MIN_PIN_CODE_LENGTH              0x0018
#define ZZB_ZCL_DOOR_LOCK_AUTO_RELOCK_TIME                 0x0023
#define ZZB_ZCL_DOOR_LOCK_OPERATING_MODE                   0x0025
#define ZZB_ZCL_DOOR_LOCK_SUPPORTED_OPERATING_MODES        0x0026
#define ZZB_ZCL_DOOR_LOCK_WRONG_CODE_ENTRY_LIMIT           0x0030
#define ZZB_ZCL_DOOR_LOCK_USER_CODE_TEMPORARY_DISABLE_TIME 0x0031
#define ZZB_ZCL_DOOR_LOCK_SEND_PIN_OVER_THE_AIR            0x0032
#define ZZB_ZCL_DOOR_LOCK_REQUIRE_PIN_FOR_RF_OPERATION     0x0033
#define ZZB_ZCL_DOOR_LOCK_ALARM_MASK                       0x0040
#define ZZB_ZCL_DOOR_LOCK_KEYPAD_OPERATION_EVENT_MASK      0x0041
#define ZZB_ZCL_DOOR_LOCK_RF_OPERATION_EVENT_MASK          0x0042
#define ZZB_ZCL_DOOR_LOCK_MANUAL_OPERATION_EVENT_MASK      0x0043
#define ZZB_ZCL_DOOR_LOCK_KEYPAD_PROGRAMMING_EVENT_MASK    0x0045
#define ZZB_ZCL_DOOR_LOCK_RF_PROGRAMMING_EVENT_MASK        0x0046

/** Commands that the server receives and a client sends (ZCL Table 7-23). */
#define ZZB_ZCL_DOOR_LOCK_LOCK_DOOR           0x00
#define ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR         0x01
#define ZZB_ZCL_DOOR_LOCK_TOGGLE              0x02
#define ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT 0x03
#define ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE        0x05
#define ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE        0x06
#define ZZB_ZCL_DOOR_LOCK_CLEAR_PIN_CODE      0x07
#define ZZB_ZCL_DOOR_LOCK_CLEAR_ALL_PIN_CODES 0x08
#define ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS     0x09
#define ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS     0x0a
#define ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE       0x14
#define ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE       0x15

/** Commands that the server generates (ZCL Table 7-27); a response has the ID of its command. */
#define ZZB_ZCL_DOOR_LOCK_LOCK_DOOR_RSP           0x00
#define ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR_RSP         0x01
#define ZZB_ZCL_DOOR_LOCK_TOGGLE_RSP              0x02
#define ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT_RSP 0x03
#define ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE_RSP        0x05
#define ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE_RSP        0x06
#define ZZB_ZCL_DOOR_LOCK_CLEAR_PIN_CODE_RSP      0x07
#define ZZB_ZCL_DOOR_LOCK_CLEAR_ALL_PIN_CODES_RSP 0x08
#define ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS_RSP     0x09
#define ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS_RSP     0x0a
#define ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE_RSP       0x14
#define ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE_RSP       0x15

/** LockState (ZCL Table 7-9). */
enum zzb_zcl_door_lock_state {
	ZZB_ZCL_DOOR_LOCK_NOT_FULLY_LOCKED = 0x00,
	ZZB_ZCL_DOOR_LOCK_LOCKED = 0x01,
	ZZB_ZCL_DOOR_LOCK_UNLOCKED = 0x02,
	ZZB_ZCL_DOOR_LOCK_UNDEFINED = 0xff,
};

/** OperatingMode (ZCL Table 7-15). */
enum zzb_zcl_door_lock_mode {
	ZZB_ZCL_DOOR_LOCK_MODE_NORMAL = 0x00,
	ZZB_ZCL_DOOR_LOCK_MODE_VACATION = 0x01,
	ZZB_ZCL_DOOR_LOCK_MODE_PRIVACY = 0x02,
	ZZB_ZCL_DOOR_LOCK_MODE_NO_RF_LOCK_UNLOCK = 0x03,
	ZZB_ZCL_DOOR_LOCK_MODE_PASSAGE = 0x04,
};

/** User Status (ZCL Table 7-24). */
enum zzb_zcl_door_lock_user_status {
	ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE = 0x00,
	ZZB_ZCL_DOOR_LOCK_USER_ENABLED = 0x01,
	ZZB_ZCL_DOOR_LOCK_USER_DISABLED = 0x03,
};

/** User Type (ZCL Table 7-25); the stack has no schedules, so it takes 0, 3 and 4. */
enum zzb_zcl_door_lock_user_type {
	ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED = 0x00,
	ZZB_ZCL_DOOR_LOCK_USER_YEAR_DAY_SCHEDULE = 0x01,
	ZZB_ZCL_DOOR_LOCK_USER_WEEK_DAY_SCHEDULE = 0x02,
	ZZB_ZCL_DOOR_LOCK_USER_MASTER = 0x03,
	ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS = 0x04,
	ZZB_ZCL_DOOR_LOCK_USER_NOT_SUPPORTED = 0xff,
};

/** The longest PIN code that the stack stores, in octets. */
#define ZZB_ZCL_DOOR_LOCK_PIN_MAX 8

/** A user of a Door Lock server; an available user has all fields 0. */
struct zzb_zcl_door_lock_user {
	/** enum zzb_zcl_door_lock_user_status. */
	uint8_t status;
	/** enum zzb_zcl_door_lock_user_type. */
	uint8_t type;
	/** Octets of code. */
	uint8_t len;
	uint8_t code[ZZB_ZCL_DOOR_LOCK_PIN_MAX];
};

/**
 * Values of a Door Lock server (ZCL 7.3.2.9). The application fills the fields up to
 * supported_operating_modes before zzb_node_init() and changes them with zzb_zcl_attr_set(); they
 * are read-only. The stack keeps the other fields, the writable attributes, and saves them.
 *
 * Lock Door, Unlock Door, Toggle and Unlock with Timeout are refused with FAILURE while
 * ActuatorEnabled is FALSE or OperatingMode is Privacy or No RF Lock or Unlock. Otherwise Toggle
 * becomes Unlock Door while LockState is Locked and Lock Door in the other states, Unlock with
 * Timeout becomes Unlock Door, and the command goes to zcl_cmd_cb with the received code as its
 * payload, an octet string, or without a payload. The application drives the bolt and sets
 * LockState. The stack answers with the response of the received command: SUCCESS when zcl_cmd_cb
 * returns SUCCESS, otherwise FAILURE.
 *
 * The stack processes the PIN code, User Status and User Type commands, keeps the users in the
 * field users and saves them. The application reads them on the stack thread, such as in a
 * callback, to check the codes of its keypad, and counts the wrong codes of its keypad itself.
 *
 * While RequirePINforRFOperation is TRUE, the RF commands need the code of a user with status
 * Enabled and a type other than Non Access; a wrong or missing code gives FAILURE and counts. When
 * WrongCodeEntryLimit is above 0 and the count reaches it, the RF commands get FAILURE for
 * UserCodeTemporaryDisableTime seconds, and alarm 0x04 occurs through the Alarms server of the
 * endpoint. A right code and the end of this lockout clear the count.
 *
 * Unlock with Timeout that zcl_cmd_cb accepts sets a relock deadline after its timeout; a timeout
 * of 0 acts as Unlock Door. When LockState becomes Unlocked, the stack sets the deadline after
 * AutoRelockTime seconds unless AutoRelockTime is 0 or a deadline is set; when it becomes Locked,
 * the deadline is removed. At the deadline zcl_cmd_cb receives Lock Door without a payload.
 */
struct zzb_zcl_door_lock {
	/** enum zzb_zcl_door_lock_state. */
	uint8_t lock_state;
	/** ZCL Table 7-10. */
	uint8_t lock_type;
	bool actuator_enabled;
	/** ZCL Table 7-12. */
	uint8_t door_state;
	/** Octets, at most ZZB_ZCL_DOOR_LOCK_PIN_MAX; the defaults of ZCL are 8 and 4. */
	uint8_t max_pin_code_length;
	uint8_t min_pin_code_length;
	/** ZCL Table 7-16: a supported mode has its bit cleared. */
	uint16_t supported_operating_modes;
	/** Seconds, 0 disabled. */
	uint32_t auto_relock_time;
	/** enum zzb_zcl_door_lock_mode. */
	uint8_t operating_mode;
	uint8_t wrong_code_entry_limit;
	/** Seconds. */
	uint8_t user_code_temporary_disable_time;
	bool send_pin_over_the_air;
	bool require_pin_for_rf_operation;
	/** Bit n enables alarm code n of ZCL Table 7-22; default 0. */
	uint16_t alarm_mask;
	/** ZCL Tables 7-30 to 7-32, 7-36 and 7-37; default 0xffff. */
	uint16_t keypad_operation_event_mask;
	uint16_t rf_operation_event_mask;
	uint16_t manual_operation_event_mask;
	uint16_t keypad_programming_event_mask;
	uint16_t rf_programming_event_mask;
	/** The users, element n with user ID n. */
	struct zzb_zcl_door_lock_user users[CONFIG_ZZB_ZCL_DOOR_LOCK_USERS];
	/** Internal: wrong codes of the RF commands since the last right code or lockout. */
	uint8_t wrong_codes;
	/** Internal: k_uptime_get() at the end of the lockout, 0 while no lockout runs. */
	int64_t lockout_end;
	/** Internal: k_uptime_get() at which the stack locks the door, 0 without a deadline. */
	int64_t relock;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_door_lock_def;

/** A Door Lock server with the values v, a pointer to struct zzb_zcl_door_lock. */
#define ZZB_ZCL_DOOR_LOCK_SERVER(v)                                                                \
	{                                                                                          \
		.id = ZZB_ZCL_DOOR_LOCK,                                                           \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_door_lock *)NULL),                            \
		.def = &zzb_zcl_door_lock_def,                                                     \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_DOOR_LOCK_H_ */
