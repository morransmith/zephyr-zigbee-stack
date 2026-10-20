/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/door_lock.h>

#include "common/node.h"
#include "fakes.h"

/* The values that the application set on endpoint 100 */
static struct zzb_zcl_door_lock factory;

static void *setup(void)
{
	test_node();
	factory = lock100;
	return NULL;
}

/* Each test starts with the values of fakes.c, the defaults and without records */
static void before(void *fixture)
{
	reset_node();
	lock100 = factory;
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the Door Lock command id with the payload to endpoint 100 */
static void command(uint8_t id, const uint8_t *payload, size_t len)
{
	uint8_t frame[32] = {0x01, ++tsn, id};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	receive_ep(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, frame, 3 + len);
}

/* Checks that frame i is the response id with the payload to the last command */
static void check_rsp(int i, uint8_t id, const uint8_t *payload, size_t len)
{
	uint8_t rsp[32] = {0x19, tsn, id};

	memcpy(rsp + 3, payload, len);
	check_frame(i, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, rsp, 3 + len);
}

/* Checks that frame i is the Default Response with status to the last command, id */
static void check_default_rsp(int i, uint8_t id, uint8_t status)
{
	const uint8_t rsp[] = {0x18, tsn, 0x0b, id, status};

	check_frame(i, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, rsp, sizeof(rsp));
}

/* Sends the command id with the payload and checks that its response with status follows */
static void operate(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	int frames = zzb_apsde_data_req_fake.call_count;

	command(id, payload, len);
	check_rsp(frames, id, &status, 1);
}

/* Checks that call i of zcl_cmd_cb got the command id of endpoint 100 with the payload */
static void check_cmd(int i, uint8_t id, const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[i];

	zassert_true(zcl_cmd_calls.count > i);
	zassert_equal(cmd->ep, DOOR_LOCK_EP);
	zassert_equal(cmd->cluster, ZZB_ZCL_DOOR_LOCK);
	zassert_equal(cmd->id, id);
	zassert_equal(cmd->len, len);
	zassert_mem_equal(zcl_cmd_calls.payload[i], payload, len);
}

/* The code "1234" as an octet string */
static const uint8_t code[] = {0x04, 0x31, 0x32, 0x33, 0x34};

/*
 * ZCL 7.3.2.11 to 7.3.2.14: the numbers of users are CONFIG_ZZB_ZCL_DOOR_LOCK_USERS, OperatingMode
 * above 4 gives INVALID_VALUE, the writable attributes are saved, and the reset restores their
 * defaults, 0xffff for the event masks
 */
ZTEST(zzb_zcl_door_lock, test_attributes)
{
	static const uint8_t read[] = {0x00, 0x60, 0x00, 0x11, 0x00, 0x12, 0x00};
	static const uint8_t read_rsp[] = {0x18, 0x60, 0x01, 0x11, 0x00, 0x00, 0x21, 0x08,
					   0x00, 0x12, 0x00, 0x00, 0x21, 0x08, 0x00};
	static const uint8_t write_bad[] = {0x00, 0x61, 0x02, 0x25, 0x00, 0x30, 0x05};
	static const uint8_t write_bad_rsp[] = {0x18, 0x61, 0x04, 0x87, 0x25, 0x00};
	static const uint8_t write[] = {0x00, 0x62, 0x02, 0x23, 0x00, 0x23, 0x1e, 0x00, 0x00, 0x00,
					0x25, 0x00, 0x30, 0x04, 0x42, 0x00, 0x19, 0x06, 0x00};
	static const uint16_t changed[] = {
		ZZB_ZCL_DOOR_LOCK_AUTO_RELOCK_TIME,
		ZZB_ZCL_DOOR_LOCK_OPERATING_MODE,
		ZZB_ZCL_DOOR_LOCK_RF_OPERATION_EVENT_MASK,
	};

	BUILD_ASSERT(CONFIG_ZZB_ZCL_DOOR_LOCK_USERS == 8);
	receive_ep(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, read, sizeof(read));
	check_frame(0, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, read_rsp, sizeof(read_rsp));

	receive_ep(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, write_bad, sizeof(write_bad));
	check_frame(1, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, write_bad_rsp, sizeof(write_bad_rsp));

	receive_ep(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, write, sizeof(write));
	check_frame(2, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, (uint8_t[]){0x18, 0x62, 0x04, 0x00}, 4);
	zassert_ok(reboot());
	zassert_equal(lock100.auto_relock_time, 30);
	zassert_equal(lock100.operating_mode, ZZB_ZCL_DOOR_LOCK_MODE_PASSAGE);
	zassert_equal(lock100.rf_operation_event_mask, 0x0006);

	reset_fakes();
	reset_node();
	check_zcl_cb(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, changed, ARRAY_SIZE(changed));
	zassert_equal(lock100.auto_relock_time, 0);
	zassert_equal(lock100.operating_mode, ZZB_ZCL_DOOR_LOCK_MODE_NORMAL);
	zassert_equal(lock100.alarm_mask, 0);
	zassert_equal(lock100.keypad_operation_event_mask, 0xffff);
	zassert_equal(lock100.rf_operation_event_mask, 0xffff);
	zassert_equal(lock100.manual_operation_event_mask, 0xffff);
	zassert_equal(lock100.keypad_programming_event_mask, 0xffff);
	zassert_equal(lock100.rf_programming_event_mask, 0xffff);
}

/*
 * ZCL 7.3.2.15.1, 7.3.2.15.2, 7.3.2.16.1: Lock Door and Unlock Door reach zcl_cmd_cb with the
 * received code or without a payload, and their responses carry SUCCESS when zcl_cmd_cb returns
 * SUCCESS, otherwise FAILURE. No Default Response follows.
 */
ZTEST(zzb_zcl_door_lock, test_lock_unlock)
{
	const struct zzb_zcl_cmd *cmd = &zcl_cmd_calls.cmd[0];

	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_cmd(0, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0);
	zassert_equal(cmd->src_addr, REQUESTER_ADDR);
	zassert_equal(cmd->src_ep, REQUESTER_EP);
	zassert_equal(cmd->tsn, tsn);
	zassert_true(cmd->unicast);
	zassert_true(cmd->server);

	zcl_cmd_status = ZZB_ZCL_FAILURE;
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, code, sizeof(code), ZZB_ZCL_FAILURE);
	check_cmd(1, ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, code, sizeof(code));

	zcl_cmd_status = ZZB_ZCL_INVALID_FIELD;
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code), ZZB_ZCL_FAILURE);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
}

/*
 * Toggle becomes Unlock Door while LockState is Locked and Lock Door in the other states, Unlock
 * with Timeout becomes Unlock Door with the code after the timeout, and each gets its own response
 */
ZTEST(zzb_zcl_door_lock, test_toggle_unlock_with_timeout)
{
	static const uint8_t timeout[] = {0x0a, 0x00, 0x04, 0x31, 0x32, 0x33, 0x34};

	operate(ZZB_ZCL_DOOR_LOCK_TOGGLE, NULL, 0, ZZB_ZCL_SUCCESS);
	check_cmd(0, ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, NULL, 0);

	lock100.lock_state = ZZB_ZCL_DOOR_LOCK_UNLOCKED;
	operate(ZZB_ZCL_DOOR_LOCK_TOGGLE, code, sizeof(code), ZZB_ZCL_SUCCESS);
	check_cmd(1, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code));

	lock100.lock_state = ZZB_ZCL_DOOR_LOCK_NOT_FULLY_LOCKED;
	operate(ZZB_ZCL_DOOR_LOCK_TOGGLE, NULL, 0, ZZB_ZCL_SUCCESS);
	check_cmd(2, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0);

	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, timeout, sizeof(timeout), ZZB_ZCL_SUCCESS);
	check_cmd(3, ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, code, sizeof(code));
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, timeout, 2, ZZB_ZCL_SUCCESS);
	check_cmd(4, ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, NULL, 0);
}

/*
 * A truncated code, and Unlock with Timeout without its timeout, give MALFORMED_COMMAND; octets
 * after the code are not passed. Get Log Record and the schedule commands are not supported.
 */
ZTEST(zzb_zcl_door_lock, test_malformed)
{
	command(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code) - 1);
	check_default_rsp(0, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, ZZB_ZCL_MALFORMED_COMMAND);
	command(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, (uint8_t[]){0x0a}, 1);
	check_default_rsp(1, ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, ZZB_ZCL_MALFORMED_COMMAND);
	command(0x04, (uint8_t[]){0x00, 0x00}, 2);
	check_default_rsp(2, 0x04, ZZB_ZCL_UNSUP_COMMAND);
	command(0x0b, NULL, 0);
	check_default_rsp(3, 0x0b, ZZB_ZCL_UNSUP_COMMAND);
	zassert_equal(zcl_cmd_calls.count, 0);

	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, (uint8_t[]){0x01, 0x31, 0xaa}, 3, ZZB_ZCL_SUCCESS);
	check_cmd(0, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, (uint8_t[]){0x01, 0x31}, 2);
}

/*
 * ZCL 7.3.2.10.3, Table 7-15: while ActuatorEnabled is FALSE or OperatingMode is Privacy or No RF
 * Lock or Unlock, the commands get FAILURE without zcl_cmd_cb; Vacation and Passage accept them
 */
ZTEST(zzb_zcl_door_lock, test_refused)
{
	static const uint8_t modes[] = {
		ZZB_ZCL_DOOR_LOCK_MODE_PRIVACY,
		ZZB_ZCL_DOOR_LOCK_MODE_NO_RF_LOCK_UNLOCK,
	};
	static const uint8_t timeout[] = {0x0a, 0x00};

	lock100.actuator_enabled = false;
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, code, sizeof(code), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_TOGGLE, NULL, 0, ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, timeout, sizeof(timeout), ZZB_ZCL_FAILURE);

	lock100.actuator_enabled = true;
	ARRAY_FOR_EACH(modes, i) {
		lock100.operating_mode = modes[i];
		operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_FAILURE);
		operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, timeout, sizeof(timeout),
			ZZB_ZCL_FAILURE);
	}

	zassert_equal(zcl_cmd_calls.count, 0);
	reset_fakes();
	lock100.operating_mode = ZZB_ZCL_DOOR_LOCK_MODE_VACATION;
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_SUCCESS);
	lock100.operating_mode = ZZB_ZCL_DOOR_LOCK_MODE_PASSAGE;
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_equal(zcl_cmd_calls.count, 2);
}

/*
 * The application can answer the copy with zzb_zcl_cmd_respond(), and then the stack sends nothing.
 * A broadcast reaches zcl_cmd_cb without a response.
 */
ZTEST(zzb_zcl_door_lock, test_respond)
{
	const struct zzb_apsde_ind broadcast = {
		.dst_addr = 0xffff,
		.dst_ep = DOOR_LOCK_EP,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_DOOR_LOCK,
	};

	zcl_cmd_responds = true;
	command(ZZB_ZCL_DOOR_LOCK_TOGGLE, NULL, 0);
	zassert_ok(zcl_cmd_respond_err);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_rsp(0, 0x42, (uint8_t[]){0xaa}, 1);

	zcl_cmd_responds = false;
	receive_ind(&broadcast, (uint8_t[]){0x01, 0x40, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR}, 3);
	zassert_equal(zcl_cmd_calls.count, 2);
	zassert_false(zcl_cmd_calls.cmd[1].unicast);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/*
 * Sends the command id with the payload after reset_fakes() and returns the status of its
 * response, which is its only frame
 */
static uint8_t status_of(uint8_t id, const uint8_t *payload, size_t len)
{
	reset_fakes();
	command(id, payload, len);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].len, 4);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x19, tsn, id}), 3);
	return sent[0].data[3];
}

/* Set PIN Code of user id with status, type and pin; returns the status of the response */
static uint8_t set_pin(uint16_t id, uint8_t status, uint8_t type, const char *pin)
{
	uint8_t payload[16] = {id & 0xff, id >> 8, status, type, strlen(pin)};

	memcpy(payload + 5, pin, strlen(pin));
	return status_of(ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE, payload, 5 + strlen(pin));
}

/* Sends the command id with user id and value; returns the status of the response */
static uint8_t set_user(uint8_t id, uint16_t user, uint8_t value)
{
	return status_of(id, (uint8_t[]){user & 0xff, user >> 8, value}, 3);
}

/* Clear PIN Code of user; returns the status of the response */
static uint8_t clear_pin(uint16_t user)
{
	return status_of(ZZB_ZCL_DOOR_LOCK_CLEAR_PIN_CODE, (uint8_t[]){user & 0xff, user >> 8}, 2);
}

/* Sends the Get command id of user after reset_fakes() */
static void get(uint8_t id, uint16_t user)
{
	reset_fakes();
	command(id, (uint8_t[]){user & 0xff, user >> 8}, 2);
}

/* Sends the Get command id of user and checks that its response has the payload after the ID */
static void check_get(uint8_t id, uint16_t user, const uint8_t *payload, size_t len)
{
	uint8_t rsp[16] = {user & 0xff, user >> 8};

	memcpy(rsp + 2, payload, len);
	get(id, user);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_rsp(0, id, rsp, 2 + len);
}

/* Checks user id of endpoint 100 */
static void check_user(int id, uint8_t status, uint8_t type, const char *pin)
{
	const struct zzb_zcl_door_lock_user *u = &lock100.users[id];

	zassert_equal(u->status, status, "user %d", id);
	zassert_equal(u->type, type, "user %d", id);
	zassert_equal(u->len, strlen(pin), "user %d", id);
	zassert_mem_equal(u->code, pin, strlen(pin), "user %d", id);
}

/*
 * ZCL 7.3.2.15.7, 7.3.2.15.8: Set PIN Code stores the user, and changes its code without a Clear
 * PIN Code first (ZCL 7.3.2.8). The code of another user gives 3 (duplicate). Get PIN Code
 * replaces the code by 0xff octets while SendPINOverTheAir is FALSE (ZCL 7.3.2.13.3).
 */
ZTEST(zzb_zcl_door_lock, test_set_pin_code)
{
	zassert_equal(set_pin(1, ZZB_ZCL_DOOR_LOCK_USER_ENABLED,
			      ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED, "1234"),
		      0);
	check_user(1, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED, "1234");
	check_get(ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE, 1,
		  (uint8_t[]){0x01, 0x00, 0x04, 0xff, 0xff, 0xff, 0xff}, 7);

	lock100.send_pin_over_the_air = true;
	check_get(ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE, 1,
		  (uint8_t[]){0x01, 0x00, 0x04, 0x31, 0x32, 0x33, 0x34}, 7);

	zassert_equal(set_pin(1, ZZB_ZCL_DOOR_LOCK_USER_DISABLED, ZZB_ZCL_DOOR_LOCK_USER_MASTER,
			      "56781234"),
		      0);
	check_user(1, ZZB_ZCL_DOOR_LOCK_USER_DISABLED, ZZB_ZCL_DOOR_LOCK_USER_MASTER, "56781234");
	zassert_equal(set_pin(7, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS,
			      "56781234"),
		      3);
	zassert_equal(set_pin(7, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS,
			      "1234"),
		      0);
	check_user(7, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS, "1234");
}

/*
 * Section 16: Set PIN Code gives 1 for a user ID outside the table, a user status other than 1 and
 * 3, a user type 1, 2 or above 4 and a code outside MinPINCodeLength to MaxPINCodeLength, which is
 * limited to 8, and MALFORMED_COMMAND for a truncated payload. The user keeps its code.
 */
ZTEST(zzb_zcl_door_lock, test_set_pin_code_errors)
{
	static const struct {
		uint16_t id;
		uint8_t status;
		uint8_t type;
		const char *pin;
	} cases[] = {
		{8, 1, 0, "5678"}, {2, 0, 0, "5678"}, {2, 2, 0, "5678"}, {2, 1, 1, "5678"},
		{2, 1, 2, "5678"}, {2, 1, 5, "5678"}, {2, 1, 0, "567"},  {2, 1, 0, "567812345"},
	};

	zassert_equal(set_pin(2, 1, 0, "1234"), 0);
	lock100.max_pin_code_length = 10;
	ARRAY_FOR_EACH(cases, i) {
		zassert_equal(set_pin(cases[i].id, cases[i].status, cases[i].type, cases[i].pin), 1,
			      "case %zu", i);
	}

	reset_fakes();
	command(ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE, (uint8_t[]){0x02, 0x00, 0x01, 0x00, 0x04, 0x35}, 6);
	check_default_rsp(0, ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE, ZZB_ZCL_MALFORMED_COMMAND);
	command(ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE, (uint8_t[]){0x02, 0x00, 0x01, 0x00}, 4);
	check_default_rsp(1, ZZB_ZCL_DOOR_LOCK_SET_PIN_CODE, ZZB_ZCL_MALFORMED_COMMAND);
	check_user(2, 1, 0, "1234");
}

/*
 * ZCL 7.3.2.16.7, 7.3.2.16.11, 7.3.2.16.22: an available user has status 0, type 0xff and an empty
 * code, and a user ID outside the table gets a Default Response with INVALID_FIELD
 */
ZTEST(zzb_zcl_door_lock, test_get_user)
{
	static const uint8_t gets[] = {
		ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE,
		ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS,
		ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE,
	};

	zassert_equal(
		set_pin(1, ZZB_ZCL_DOOR_LOCK_USER_DISABLED, ZZB_ZCL_DOOR_LOCK_USER_MASTER, "1234"),
		0);
	check_get(ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS, 1, (uint8_t[]){0x03}, 1);
	check_get(ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE, 1, (uint8_t[]){0x03}, 1);
	check_get(ZZB_ZCL_DOOR_LOCK_GET_PIN_CODE, 0, (uint8_t[]){0x00, 0xff, 0x00}, 3);
	check_get(ZZB_ZCL_DOOR_LOCK_GET_USER_STATUS, 7, (uint8_t[]){0x00}, 1);
	check_get(ZZB_ZCL_DOOR_LOCK_GET_USER_TYPE, 7, (uint8_t[]){0xff}, 1);

	ARRAY_FOR_EACH(gets, i) {
		get(gets[i], 8);
		check_default_rsp(0, gets[i], ZZB_ZCL_INVALID_FIELD);
		reset_fakes();
		command(gets[i], (uint8_t[]){0x01}, 1);
		check_default_rsp(0, gets[i], ZZB_ZCL_MALFORMED_COMMAND);
	}
}

/*
 * ZCL 7.3.2.15.9, 7.3.2.15.10: Clear PIN Code makes the user available and gives 1 for a user ID
 * outside the table; Clear All PIN Codes makes every user available
 */
ZTEST(zzb_zcl_door_lock, test_clear)
{
	zassert_equal(set_pin(1, 1, 0, "1234"), 0);
	zassert_equal(set_pin(2, 3, 3, "5678"), 0);
	zassert_equal(set_pin(3, 1, 4, "4321"), 0);
	zassert_equal(clear_pin(1), 0);
	check_user(1, 0, 0, "");
	check_user(2, 3, 3, "5678");
	zassert_equal(clear_pin(8), 1);

	zassert_equal(status_of(ZZB_ZCL_DOOR_LOCK_CLEAR_ALL_PIN_CODES, NULL, 0), 0);
	ARRAY_FOR_EACH(lock100.users, i) {
		check_user(i, 0, 0, "");
	}
}

/*
 * Section 16: Set User Status takes 1 and 3 and Set User Type 0, 3 and 4 for an occupied user;
 * other values, an available user and a user ID outside the table give 1
 */
ZTEST(zzb_zcl_door_lock, test_set_user)
{
	static const uint8_t statuses[] = {0, 2, 0xff};
	static const uint8_t types[] = {1, 2, 5, 0xff};

	zassert_equal(set_pin(1, 1, 0, "1234"), 0);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 1, 3), 0);
	check_user(1, 3, 0, "1234");
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 1, 1), 0);
	ARRAY_FOR_EACH(statuses, i) {
		zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 1, statuses[i]), 1);
	}

	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 1, 3), 0);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 1, 4), 0);
	check_user(1, 1, 4, "1234");
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 1, 0), 0);
	ARRAY_FOR_EACH(types, i) {
		zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 1, types[i]), 1);
	}

	check_user(1, 1, 0, "1234");
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 2, 1), 1);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 2, 0), 1);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 8, 1), 1);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 8, 0), 1);
	check_user(2, 0, 0, "");

	reset_fakes();
	command(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, (uint8_t[]){0x01, 0x00}, 2);
	check_default_rsp(0, ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, ZZB_ZCL_MALFORMED_COMMAND);
}

/*
 * Section 15: each change of a user is saved, a cleared user is deleted, and the reset of the node
 * clears the users and deletes their records
 */
ZTEST(zzb_zcl_door_lock, test_records)
{
	zassert_equal(set_pin(0, 1, 0, "1234"), 0);
	zassert_equal(set_pin(6, 1, 0, "12345678"), 0);
	zassert_equal(set_pin(7, 1, 0, "4321"), 0);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_STATUS, 6, 3), 0);
	zassert_equal(set_user(ZZB_ZCL_DOOR_LOCK_SET_USER_TYPE, 7, 3), 0);
	zassert_equal(clear_pin(0), 0);
	zassert_ok(reboot());
	check_user(0, 0, 0, "");
	check_user(6, 3, 0, "12345678");
	check_user(7, 1, 3, "4321");

	reset_node();
	check_user(6, 0, 0, "");
	check_user(7, 0, 0, "");
	zassert_ok(reboot());
	ARRAY_FOR_EACH(lock100.users, i) {
		check_user(i, 0, 0, "");
	}
}

ZTEST(zzb_zcl_door_lock, test_bad_records)
{
	/* Version, enabled, unrestricted, the code "1234" */
	uint8_t rec[] = {2, 1, 0, 4, 0x31, 0x32, 0x33, 0x34, 0, 0, 0, 0};

	zassert_ok(settings_save_one("zzb/0/zcl/lock/100/1", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/zcl/lock/100/1", rec, sizeof(rec) - 1));
	zassert_equal(reboot(), -EINVAL);

	/* A code of 9 octets */
	rec[3] = 9;
	zassert_ok(settings_save_one("zzb/0/zcl/lock/100/1", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);

	/* A record of an endpoint without a Door Lock server, of an unknown one or of user 8 */
	rec[3] = 4;
	zassert_ok(settings_save_one("zzb/0/zcl/lock/1/1", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/lock/9/1", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/lock/100/8", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/lock/100/1", rec, sizeof(rec)));
	zassert_ok(reboot());
	check_user(1, 1, 0, "1234");

	zassert_ok(settings_delete("zzb/0/zcl/lock/1/1"));
	zassert_ok(settings_delete("zzb/0/zcl/lock/9/1"));
	zassert_ok(settings_delete("zzb/0/zcl/lock/100/8"));
}

/* Stores user id of endpoint 100 without a command */
static void add_user(int id, uint8_t status, uint8_t type, const char *pin)
{
	struct zzb_zcl_door_lock_user *u = &lock100.users[id];

	*u = (struct zzb_zcl_door_lock_user){.status = status, .type = type, .len = strlen(pin)};
	memcpy(u->code, pin, strlen(pin));
}

static bool timer_armed(void)
{
	return k_work_delayable_is_pending(&test_node()->zcl.door_lock_timer);
}

/* The code "9999", which no user has */
static const uint8_t wrong[] = {0x04, 0x39, 0x39, 0x39, 0x39};

/*
 * ZCL 7.3.2.13.4: with RequirePINforRFOperation the code must be the code of a user with status 1
 * and a type other than 4; a disabled user, a non access user, a wrong and a missing code give
 * FAILURE without zcl_cmd_cb. WrongCodeEntryLimit 0 gives no lockout.
 */
ZTEST(zzb_zcl_door_lock, test_require_pin)
{
	static const uint8_t disabled[] = {0x05, 0x32, 0x32, 0x32, 0x32, 0x32};
	static const uint8_t non_access[] = {0x04, 0x33, 0x33, 0x33, 0x33};
	static const uint8_t master[] = {0x00, 0x00, 0x04, 0x34, 0x34, 0x34, 0x34};

	add_user(1, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED, "1234");
	add_user(2, ZZB_ZCL_DOOR_LOCK_USER_DISABLED, ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED, "22222");
	add_user(3, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_NON_ACCESS, "3333");
	add_user(4, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_MASTER, "4444");
	lock100.require_pin_for_rf_operation = true;

	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, disabled, sizeof(disabled), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, non_access, sizeof(non_access), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_TOGGLE, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	zassert_equal(zcl_cmd_calls.count, 0);

	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code), ZZB_ZCL_SUCCESS);
	check_cmd(0, ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code));
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_WITH_TIMEOUT, master, sizeof(master), ZZB_ZCL_SUCCESS);
	zassert_equal(zcl_cmd_calls.count, 2);
	zassert_false(timer_armed());
}

/*
 * ZCL 7.3.2.13.1, 7.3.2.13.2: the count of wrong codes reaching WrongCodeEntryLimit refuses the
 * commands for UserCodeTemporaryDisableTime seconds and raises alarm 0x04, which goes as Alarm with
 * bit 4 of AlarmMask (ZCL Table 7-22). A right code and the end of the lockout clear the count.
 */
ZTEST(zzb_zcl_door_lock, test_lockout)
{
	static const uint16_t alarm_count[] = {ZZB_ZCL_ALARMS_ALARM_COUNT};

	add_user(1, ZZB_ZCL_DOOR_LOCK_USER_ENABLED, ZZB_ZCL_DOOR_LOCK_USER_UNRESTRICTED, "1234");
	lock100.require_pin_for_rf_operation = true;
	lock100.wrong_code_entry_limit = 3;
	lock100.user_code_temporary_disable_time = 2;

	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code), ZZB_ZCL_SUCCESS);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_FAILURE);
	zassert_false(timer_armed());
	operate(ZZB_ZCL_DOOR_LOCK_UNLOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	zassert_true(timer_armed());
	zassert_equal(alarm_entries(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, 0x04), 1);
	check_zcl_cb(DOOR_LOCK_EP, ZZB_ZCL_ALARMS, alarm_count, ARRAY_SIZE(alarm_count));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 6);

	reset_fakes();
	k_sleep(K_MSEC(1900));
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code), ZZB_ZCL_FAILURE);
	zassert_equal(zcl_cmd_calls.count, 0);
	k_sleep(K_MSEC(200));
	zassert_false(timer_armed());

	/* The count starts again from 0 */
	lock100.alarm_mask = BIT(4);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, code, sizeof(code), ZZB_ZCL_SUCCESS);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 7);
	check_alarm(6, DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, 0x04);
	zassert_equal(alarm_entries(DOOR_LOCK_EP, ZZB_ZCL_DOOR_LOCK, 0x04), 2);
}

/* The reset of the node and a reboot end the lockout */
ZTEST(zzb_zcl_door_lock, test_lockout_reset)
{
	lock100.require_pin_for_rf_operation = true;
	lock100.wrong_code_entry_limit = 1;
	lock100.user_code_temporary_disable_time = 10;
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	zassert_true(timer_armed());
	reset_node();
	zassert_false(timer_armed());
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_SUCCESS);

	lock100.require_pin_for_rf_operation = true;
	lock100.wrong_code_entry_limit = 1;
	lock100.user_code_temporary_disable_time = 10;
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, wrong, sizeof(wrong), ZZB_ZCL_FAILURE);
	zassert_ok(reboot());
	zassert_false(timer_armed());
	operate(ZZB_ZCL_DOOR_LOCK_LOCK_DOOR, NULL, 0, ZZB_ZCL_SUCCESS);
}

ZTEST_SUITE(zzb_zcl_door_lock, NULL, setup, before, after, NULL);
