/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/door_lock.h>

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

ZTEST_SUITE(zzb_zcl_door_lock, NULL, setup, before, after, NULL);
