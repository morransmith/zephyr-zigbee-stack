/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zzb/zcl/door_lock.h>

#include "common/node.h"
#include "common/queue.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 7.3.1.1) */
#define REVISION 3

/* The event masks enable every event, unlike the 0x0000 of ZCL Table 7-21 (section 16) */
#define EVENTS_ALL 0xffff

/* Timeout in seconds, before the code of Unlock with Timeout */
#define TIMEOUT_LEN 2

/* User ID, User Status and User Type, before the code of Set PIN Code */
#define SET_PIN_CODE_LEN 4

/* The statuses of the responses to the commands of the users (ZCL 7.3.2.16.6 to 7.3.2.16.21) */
#define PASS           0x00
#define FAIL           0x01
#define DUPLICATE_CODE 0x03

/* Tamper Alarm - wrong code entry limit (ZCL Table 7-22) */
#define ALARM_WRONG_CODES 0x04

#define RECORD_VERSION 1
/* Version, user status, user type, code length and code */
#define RECORD_LEN     (4 + ZZB_ZCL_DOOR_LOCK_PIN_MAX)
/* Room for zcl/lock/<ep>/<user> */
#define NAME_LEN       20

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

static void record_name(char name[NAME_LEN], uint8_t ep, int id)
{
	snprintk(name, NAME_LEN, "zcl/lock/%u/%d", ep, id);
}

/* Saves user id of the server on ep, or deletes the record of an available user */
static void save(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_door_lock *v, int id)
{
	const struct zzb_zcl_door_lock_user *u = &v->users[id];
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION, u->status, u->type, u->len};
	char name[NAME_LEN];

	record_name(name, ep, id);
	if (u->status == ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE) {
		zzb_settings_delete(node, name);
		return;
	}

	memcpy(rec + 4, u->code, sizeof(u->code));
	zzb_settings_save(node, name, rec, sizeof(rec));
}

/* The user ID at the start of the payload, which has at least 2 octets, or -1 outside the table */
static int user_id(const struct zzb_zcl_cmd *cmd)
{
	uint16_t id = sys_get_le16(cmd->payload);

	return id < CONFIG_ZZB_ZCL_DOOR_LOCK_USERS ? id : -1;
}

static bool status_valid(uint8_t status)
{
	return status == ZZB_ZCL_DOOR_LOCK_USER_ENABLED ||
	       status == ZZB_ZCL_DOOR_LOCK_USER_DISABLED;
}

/* The schedule types need schedules, which the stack does not have */
static bool type_valid(uint8_t type)
{
	return type == ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED ||
	       type == ZZB_ZCL_DOOR_LOCK_USER_MASTER || type == ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS;
}

/* The type that Get PIN Code and Get User Type give, 0xff for an available user (ZCL 7.3.2.16.7) */
static uint8_t type_of(const struct zzb_zcl_door_lock_user *u)
{
	return u->status == ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE ? ZZB_ZCL_DOOR_LOCK_USER_NOT_SUPPORTED
							     : u->type;
}

static bool has_code(const struct zzb_zcl_door_lock_user *u, const uint8_t *code, size_t len)
{
	return u->status != ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE && u->len == len &&
	       memcmp(u->code, code, len) == 0;
}

/* Whether a user other than id has the code */
static bool duplicate(const struct zzb_zcl_door_lock *v, int id, const uint8_t *code, size_t len)
{
	for (int i = 0; i < ARRAY_SIZE(v->users); i++) {
		if (i != id && has_code(&v->users[i], code, len)) {
			return true;
		}
	}

	return false;
}

/* The ID of the user with the code, status 1 and a type other than 4 (ZCL 7.3.2.13.4), or -1 */
static int code_user(const struct zzb_zcl_door_lock *v, const uint8_t *code, size_t len)
{
	for (int i = 0; i < ARRAY_SIZE(v->users); i++) {
		const struct zzb_zcl_door_lock_user *u = &v->users[i];

		if (u->status == ZZB_ZCL_DOOR_LOCK_USER_ENABLED &&
		    u->type != ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS && has_code(u, code, len)) {
			return i;
		}
	}

	return -1;
}

/*
 * Writes the code of len octets to p as an octet string, with 0xff octets in its place while
 * SendPINOverTheAir is FALSE (ZCL 7.3.2.13.3); returns its length
 */
static size_t put_code(uint8_t *p, const struct zzb_zcl_door_lock *v, const uint8_t *code,
		       size_t len)
{
	p[0] = len;
	if (v->send_pin_over_the_air) {
		memcpy(p + 1, code, len);
	} else {
		memset(p + 1, 0xff, len);
	}

	return 1 + len;
}

/* The values of the Door Lock server on ep, or NULL */
static struct zzb_zcl_door_lock *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_DOOR_LOCK, true);

	return cl != NULL ? cl->values : NULL;
}

/* The earlier of next and a deadline, which is 0 when it is not set */
static int64_t earlier(int64_t next, int64_t deadline)
{
	return deadline != 0 ? MIN(next, deadline) : next;
}

static bool due(int64_t deadline, int64_t now)
{
	return deadline != 0 && deadline <= now;
}

/* Arms the timer for the earliest relock or end of a lockout of the node, or stops it */
static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_door_lock *v = server(&node->device->endpoints[i]);

		if (v != NULL) {
			next = earlier(earlier(next, v->lockout_end), v->relock);
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.door_lock_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.door_lock_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* Sets the relock deadline after time seconds; 0 sets none */
static void relock_after(struct zzb_node *node, struct zzb_zcl_door_lock *v, uint32_t time)
{
	if (time == 0) {
		return;
	}

	v->relock = k_uptime_get() + time * MSEC_PER_SEC;
	schedule(node);
}

/* Passes Lock Door without a payload to zcl_cmd_cb and ignores its status */
static void relock(struct zzb_node *node, uint8_t ep, struct zzb_zcl_door_lock *v)
{
	const struct zzb_zcl_cmd cmd = {
		.ep = ep,
		.cluster = ZZB_ZCL_DOOR_LOCK,
		.id = ZZB_ZCL_DOOR_LOCK_LOCK_DOOR,
		.server = true,
	};

	v->relock = 0;
	zzb_zcl_app_cmd(node, &cmd);
}

/* The end of a lockout clears the count of wrong codes */
static void tick(struct k_work *work)
{
	struct zzb_node *node = CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node,
					     zcl.door_lock_timer);
	int64_t now = k_uptime_get();

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_door_lock *v = server(ep);

		if (v == NULL) {
			continue;
		}

		if (due(v->lockout_end, now)) {
			v->lockout_end = 0;
			v->wrong_codes = 0;
		}

		if (due(v->relock, now)) {
			relock(node, ep->id, v);
		}
	}

	schedule(node);
}

/*
 * Counts a wrong code of an RF command on ep. Reaching WrongCodeEntryLimit starts the lockout and
 * raises its alarm (ZCL 7.3.2.13.1).
 */
static void wrong_code(struct zzb_node *node, uint8_t ep, struct zzb_zcl_door_lock *v)
{
	v->wrong_codes++;
	if (v->wrong_code_entry_limit == 0 || v->wrong_codes < v->wrong_code_entry_limit) {
		return;
	}

	v->lockout_end = k_uptime_get() + v->user_code_temporary_disable_time * MSEC_PER_SEC;
	schedule(node);
	zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_DOOR_LOCK, ALARM_WRONG_CODES,
			    v->alarm_mask & BIT(ALARM_WRONG_CODES), true);
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
		   struct zzb_zcl_door_lock *v)
{
	/* A copy of the whole request, which zzb_zcl_cmd_respond() can answer */
	struct zzb_zcl_req req = *CONTAINER_OF(cmd, struct zzb_zcl_req, cmd);
	size_t off = cmd->id == ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT ? TIMEOUT_LEN : 0;
	const uint8_t *code = cmd->payload + off + 1;
	int field = 0;
	int status;
	int len;

	if (cmd->len > off) {
		field = zzb_zcl_value_len(ZZB_ZCL_OCTSTR, cmd->payload + off, cmd->len - off);
	}

	if (cmd->len < off || field < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	/* ZCL Table 7-15 */
	if (!v->actuator_enabled || v->operating_mode == ZZB_ZCL_DOOR_LOCK_MODE_PRIVACY ||
	    v->operating_mode == ZZB_ZCL_DOOR_LOCK_MODE_NO_RF_LOCK_UNLOCK || v->lockout_end != 0) {
		return respond(node, cmd, ZZB_ZCL_FAILURE);
	}

	/* The code without its length octet; a missing code is empty */
	len = MAX(field - 1, 0);
	if (v->require_pin_for_rf_operation && code_user(v, code, len) < 0) {
		status = respond(node, cmd, ZZB_ZCL_FAILURE);
		wrong_code(node, cmd->ep, v);
		return status;
	}

	/* A right code clears the count, which only wrong codes raise */
	v->wrong_codes = 0;
	req.cmd.id = resolve(cmd, v);
	req.cmd.payload = cmd->payload + off;
	req.cmd.len = field;
	status = zzb_zcl_app_cmd(node, &req.cmd);
	if (status == ZZB_ZCL_SUCCESS && cmd->id == ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT) {
		relock_after(node, v, sys_get_le16(cmd->payload));
	}

	if (status == ZZB_ZCL_RESPONDED) {
		return status;
	}

	return respond(node, cmd, status == ZZB_ZCL_SUCCESS ? ZZB_ZCL_SUCCESS : ZZB_ZCL_FAILURE);
}

/* ZCL 7.3.2.15.7; a code length of 0xff is the empty string */
static int set_pin_code(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			struct zzb_zcl_door_lock *v)
{
	const uint8_t *pin = cmd->payload + SET_PIN_CODE_LEN;
	struct zzb_zcl_door_lock_user *u;
	int field;
	int len;
	int id;

	if (cmd->len < SET_PIN_CODE_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	field = zzb_zcl_value_len(ZZB_ZCL_OCTSTR, pin, cmd->len - SET_PIN_CODE_LEN);
	if (field < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	id = user_id(cmd);
	len = field - 1;
	if (id < 0 || !status_valid(cmd->payload[2]) || !type_valid(cmd->payload[3]) ||
	    !IN_RANGE(len, v->min_pin_code_length,
		      MIN(v->max_pin_code_length, ZZB_ZCL_DOOR_LOCK_PIN_MAX))) {
		return respond(node, cmd, FAIL);
	}

	if (duplicate(v, id, pin + 1, len)) {
		return respond(node, cmd, DUPLICATE_CODE);
	}

	u = &v->users[id];
	*u = (struct zzb_zcl_door_lock_user){
		.status = cmd->payload[2],
		.type = cmd->payload[3],
		.len = len,
	};
	memcpy(u->code, pin + 1, len);
	save(node, cmd->ep, v, id);
	return respond(node, cmd, PASS);
}

/*
 * Get PIN Code, Get User Status and Get User Type (ZCL 7.3.2.16.7, 7.3.2.16.11, 7.3.2.16.22). A
 * user ID outside the table gets a Default Response with an error.
 */
static int get_user(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		    const struct zzb_zcl_door_lock *v)
{
	uint8_t rsp[5 + ZZB_ZCL_DOOR_LOCK_PIN_MAX];
	const struct zzb_zcl_door_lock_user *u;
	size_t len = 3;
	int id;

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	id = user_id(cmd);
	if (id < 0) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	u = &v->users[id];
	sys_put_le16(id, rsp);
	switch (cmd->id) {
	case ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE:
		rsp[2] = u->status;
		rsp[3] = type_of(u);
		len = 4 + put_code(rsp + 4, v, u->code, u->len);
		break;
	case ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS:
		rsp[2] = u->status;
		break;
	default:
		rsp[2] = type_of(u);
		break;
	}

	zzb_zcl_cmd_reply(node, cmd, cmd->id, rsp, len);
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 7.3.2.15.9: the user becomes available, with the default type */
static int clear_pin_code(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			  struct zzb_zcl_door_lock *v)
{
	int id;

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	id = user_id(cmd);
	if (id < 0) {
		return respond(node, cmd, FAIL);
	}

	v->users[id] = (struct zzb_zcl_door_lock_user){0};
	save(node, cmd->ep, v, id);
	return respond(node, cmd, PASS);
}

/* Makes every user of the server on ep available and deletes the records */
static void clear_users(struct zzb_node *node, uint8_t ep, struct zzb_zcl_door_lock *v)
{
	for (int i = 0; i < ARRAY_SIZE(v->users); i++) {
		v->users[i] = (struct zzb_zcl_door_lock_user){0};
		save(node, ep, v, i);
	}
}

/* ZCL 7.3.2.15.11; an available user takes Set PIN Code instead */
static int set_user_status(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			   struct zzb_zcl_door_lock *v)
{
	int id;

	if (cmd->len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	id = user_id(cmd);
	if (id < 0 || v->users[id].status == ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE ||
	    !status_valid(cmd->payload[2])) {
		return respond(node, cmd, FAIL);
	}

	v->users[id].status = cmd->payload[2];
	save(node, cmd->ep, v, id);
	return respond(node, cmd, PASS);
}

/* ZCL 7.3.2.15.22, of an occupied user */
static int set_user_type(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_door_lock *v)
{
	int id;

	if (cmd->len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	id = user_id(cmd);
	if (id < 0 || v->users[id].status == ZZB_ZCL_DOOR_LOCK_USER_AVAILABLE ||
	    !type_valid(cmd->payload[2])) {
		return respond(node, cmd, FAIL);
	}

	v->users[id].type = cmd->payload[2];
	save(node, cmd->ep, v, id);
	return respond(node, cmd, PASS);
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	struct zzb_zcl_door_lock *v = values;

	switch (cmd->id) {
	case ZZB_ZCL_DOOR_LOCK_LOCK_DOOR:
	case ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR:
	case ZZB_ZCL_DOOR_LOCK_TOGGLE:
	case ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT:
		return operate(node, cmd, v);
	case ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE:
		return set_pin_code(node, cmd, v);
	case ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE:
	case ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS:
	case ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE:
		return get_user(node, cmd, v);
	case ZZB_ZCL_DOOR_LOCK_CLEAR_PIN_CODE:
		return clear_pin_code(node, cmd, v);
	case ZZB_ZCL_DOOR_LOCK_CLEAR_ALL_PIN_CODES:
		clear_users(node, cmd->ep, v);
		return respond(node, cmd, PASS);
	case ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS:
		return set_user_status(node, cmd, v);
	case ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE:
		return set_user_type(node, cmd, v);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* ZCL 7.3.2.12.6: an unlock from any source starts AutoRelockTime, unless a deadline is set */
static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	struct zzb_zcl_door_lock *v = values;

	if (attr != ZZB_ZCL_DOOR_LOCK_LOCK_STATE) {
		return;
	}

	if (v->lock_state == ZZB_ZCL_DOOR_LOCK_LOCKED) {
		v->relock = 0;
		schedule(node);
	} else if (v->lock_state == ZZB_ZCL_DOOR_LOCK_UNLOCKED && v->relock == 0) {
		relock_after(node, v, v->auto_relock_time);
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

/* The fields of the application keep their values; zzb_zcl_init() has stopped the timer */
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
	memset(v->users, 0, sizeof(v->users));
	v->wrong_codes = 0;
	v->lockout_end = 0;
	v->relock = 0;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_DOOR_LOCK, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_door_lock *v = values;

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
	clear_users(node, ep, v);
	v->wrong_codes = 0;
	v->lockout_end = 0;
	v->relock = 0;
	schedule(node);
}

const struct zzb_zcl_cluster_def zzb_zcl_door_lock_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
	.changed = changed,
};

void zzb_zcl_door_lock_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.door_lock_timer, tick);
}

int zzb_zcl_door_lock_load(struct zzb_node *node, const char *key, size_t len,
			   settings_read_cb read_cb, void *cb_arg)
{
	const struct zzb_endpoint *e;
	struct zzb_zcl_door_lock_user *u;
	struct zzb_zcl_door_lock *v;
	uint8_t rec[RECORD_LEN];
	unsigned long ep, id;
	char *end;
	int err;

	/* <ep>/<user> */
	ep = strtoul(key, &end, 10);
	id = *end == '/' ? strtoul(end + 1, &end, 10) : ULONG_MAX;
	e = ep <= UINT8_MAX ? zzb_node_endpoint(node, ep) : NULL;
	v = e != NULL ? server(e) : NULL;
	if (v == NULL || id >= ARRAY_SIZE(v->users)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	if (rec[3] > ZZB_ZCL_DOOR_LOCK_PIN_MAX) {
		return -EINVAL;
	}

	u = &v->users[id];
	u->status = rec[1];
	u->type = rec[2];
	u->len = rec[3];
	memcpy(u->code, rec + 4, sizeof(u->code));
	return 0;
}
