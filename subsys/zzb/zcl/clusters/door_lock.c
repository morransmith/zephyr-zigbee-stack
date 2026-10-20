/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/door_lock.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 7.3.1.1) */
#define REVISION 3

/* The event masks enable every event, unlike the 0x0000 of ZCL Table 7-21 (section 16) */
#define EVENTS_ALL 0xffff

/* Timeout in seconds, before the code of Unlock with Timeout */
#define TIMEOUT_LEN 2

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_LOCK_STATE, ZZB_ZCL_ENUM8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_door_lock, lock_state),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_LOCK_TYPE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_door_lock,
		     lock_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_ACTUATOR_ENABLED, ZZB_ZCL_BOOL, 0, struct zzb_zcl_door_lock,
		     actuator_enabled),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_DOOR_STATE, ZZB_ZCL_ENUM8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_door_lock, door_state),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_DOOR_LOCK_NUMBER_OF_TOTAL_USERS_SUPPORTED, ZZB_ZCL_UINT16,
			   CONFIG_ZZB_ZCL_DOOR_LOCK_USERS),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_DOOR_LOCK_NUMBER_OF_PIN_USERS_SUPPORTED, ZZB_ZCL_UINT16,
			   CONFIG_ZZB_ZCL_DOOR_LOCK_USERS),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_MAX_PIN_CODE_LENGTH, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_door_lock, max_pin_code_length),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_MIN_PIN_CODE_LENGTH, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_door_lock, min_pin_code_length),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_AUTO_RELOCK_TIME, ZZB_ZCL_UINT32,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     auto_relock_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_OPERATING_MODE, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     operating_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_SUPPORTED_OPERATING_MODES, ZZB_ZCL_MAP16, 0,
		     struct zzb_zcl_door_lock, supported_operating_modes),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_WRONG_CODE_ENTRY_LIMIT, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     wrong_code_entry_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_USER_CODE_TEMPORARY_DISABLE_TIME, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     user_code_temporary_disable_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_SEND_PIN_OVER_THE_AIR, ZZB_ZCL_BOOL,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     send_pin_over_the_air),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_REQUIRE_PIN_FOR_RF_OPERATION, ZZB_ZCL_BOOL,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     require_pin_for_rf_operation),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_ALARM_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     alarm_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_KEYPAD_OPERATION_EVENT_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     keypad_operation_event_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_RF_OPERATION_EVENT_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     rf_operation_event_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_MANUAL_OPERATION_EVENT_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     manual_operation_event_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_KEYPAD_PROGRAMMING_EVENT_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     keypad_programming_event_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DOOR_LOCK_RF_PROGRAMMING_EVENT_MASK, ZZB_ZCL_MAP16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_door_lock,
		     rf_programming_event_mask),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* Sends the response of cmd, which has the ID of cmd, with status to a unicast (ZCL 7.3.2.16) */
static int respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t status)
{
	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	zzb_zcl_cmd_reply(node, cmd, cmd->id, &status, 1);
	return ZZB_ZCL_RESPONDED;
}

/* Toggle by LockState and Unlock with Timeout become Lock Door or Unlock Door */
static uint8_t resolve(const struct zzb_zcl_cmd *cmd, const struct zzb_zcl_door_lock *v)
{
	switch (cmd->id) {
	case ZZB_ZCL_DOOR_LOCK_TOGGLE:
		return v->lock_state == ZZB_ZCL_DOOR_LOCK_LOCKED ? ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR
								 : ZZB_ZCL_DOOR_LOCK_LOCK_DOOR;
	case ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT:
		return ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR;
	default:
		return cmd->id;
	}
}

/* Lock Door, Unlock Door, Toggle and Unlock with Timeout (ZCL 7.3.2.15.1 to 7.3.2.15.4) */
static int operate(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		   const struct zzb_zcl_door_lock *v)
{
	/* A copy of the whole request, which zzb_zcl_cmd_respond() can answer */
	struct zzb_zcl_req req = *CONTAINER_OF(cmd, struct zzb_zcl_req, cmd);
	size_t off = cmd->id == ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT ? TIMEOUT_LEN : 0;
	int code_len = 0;
	int status;

	if (cmd->len > off) {
		code_len = zzb_zcl_value_len(ZZB_ZCL_OCTSTR, cmd->payload + off, cmd->len - off);
	}

	if (cmd->len < off || code_len < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	/* ZCL Table 7-15 */
	if (!v->actuator_enabled || v->operating_mode == ZZB_ZCL_DOOR_LOCK_MODE_PRIVACY ||
	    v->operating_mode == ZZB_ZCL_DOOR_LOCK_MODE_NO_RF_LOCK_UNLOCK) {
		return respond(node, cmd, ZZB_ZCL_FAILURE);
	}

	req.cmd.id = resolve(cmd, v);
	req.cmd.payload = cmd->payload + off;
	req.cmd.len = code_len;
	status = zzb_zcl_app_cmd(node, &req.cmd);
	if (status == ZZB_ZCL_RESPONDED) {
		return status;
	}

	return respond(node, cmd, status == ZZB_ZCL_SUCCESS ? ZZB_ZCL_SUCCESS : ZZB_ZCL_FAILURE);
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_DOOR_LOCK_LOCK_DOOR:
	case ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR:
	case ZZB_ZCL_DOOR_LOCK_TOGGLE:
	case ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT:
		return operate(node, cmd, values);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	if (attr->id == ZZB_ZCL_DOOR_LOCK_OPERATING_MODE &&
	    value > ZZB_ZCL_DOOR_LOCK_MODE_PASSAGE) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

/* The fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_door_lock *v = values;

	v->auto_relock_time = 0;
	v->operating_mode = ZZB_ZCL_DOOR_LOCK_MODE_NORMAL;
	v->wrong_code_entry_limit = 0;
	v->user_code_temporary_disable_time = 0;
	v->send_pin_over_the_air = false;
	v->require_pin_for_rf_operation = false;
	v->alarm_mask = 0;
	v->keypad_operation_event_mask = EVENTS_ALL;
	v->rf_operation_event_mask = EVENTS_ALL;
	v->manual_operation_event_mask = EVENTS_ALL;
	v->keypad_programming_event_mask = EVENTS_ALL;
	v->rf_programming_event_mask = EVENTS_ALL;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_DOOR_LOCK, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_DOOR_LOCK_AUTO_RELOCK_TIME, 0);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_OPERATING_MODE, ZZB_ZCL_DOOR_LOCK_MODE_NORMAL);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_WRONG_CODE_ENTRY_LIMIT, 0);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_USER_CODE_TEMPORARY_DISABLE_TIME, 0);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_SEND_PIN_OVER_THE_AIR, false);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_REQUIRE_PIN_FOR_RF_OPERATION, false);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_ALARM_MASK, 0);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_KEYPAD_OPERATION_EVENT_MASK, EVENTS_ALL);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_RF_OPERATION_EVENT_MASK, EVENTS_ALL);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_MANUAL_OPERATION_EVENT_MASK, EVENTS_ALL);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_KEYPAD_PROGRAMMING_EVENT_MASK, EVENTS_ALL);
	set(node, ep, ZZB_ZCL_DOOR_LOCK_RF_PROGRAMMING_EVENT_MASK, EVENTS_ALL);
}

const struct zzb_zcl_cluster_def zzb_zcl_door_lock_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
};
