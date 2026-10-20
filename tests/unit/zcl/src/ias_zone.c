/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/ias_zone.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

/* The IEEE address of the CIE, which writes it from this endpoint */
#define CIE_EUI64  0x0011223344556677
#define CIE_SRC_EP 0x0b

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts without a CIE, without records and with ZoneStatus 0 */
static void before(void *fixture)
{
	zone80.zone_status = 0;
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Writes IAS_CIE_Address of endpoint 80 from endpoint src_ep of the requester */
static void write_cie(uint64_t addr, uint8_t src_ep)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ZONE_EP,
		.src_addr = REQUESTER_ADDR,
		.src_ep = src_ep,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_IAS_ZONE,
	};
	uint8_t req[14] = {0x00, ++tsn, 0x02, 0x10, 0x00, 0xf0};
	int i = zzb_apsde_data_req_fake.call_count;

	sys_put_le64(addr, req + 6);
	receive_ind(&ind, req, sizeof(req));
	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(sent[i].req.dst_ep, src_ep);
	zassert_mem_equal(sent[i].data, ((uint8_t[]){0x18, tsn, 0x04, 0x00}), 4);
}

/*
 * Sends the command id with the payload from the requester to endpoint 80 and returns the status of
 * its Default Response, the last frame
 */
static uint8_t command(uint8_t id, const uint8_t *payload, size_t len)
{
	uint8_t frame[8] = {0x01, ++tsn, id};
	int last;

	memcpy(frame + 3, payload, len);
	receive_ep(ZONE_EP, ZZB_ZCL_IAS_ZONE, frame, 3 + len);
	last = zzb_apsde_data_req_fake.call_count - 1;
	zassert_true(last >= 0);
	zassert_mem_equal(sent[last].data, ((uint8_t[]){0x18, tsn, 0x0b, id}), 4);
	return sent[last].data[4];
}

static uint8_t enroll_response(uint8_t code, uint8_t zone)
{
	return command(ZZB_ZCL_IAS_ZONE_ENROLL_RSP, (uint8_t[]){code, zone}, 2);
}

/* Checks that frame i is the command id of endpoint 80 to the CIE with the payload */
static void check_zone_cmd(int i, uint8_t id, const uint8_t *payload, size_t len)
{
	const struct frame_copy *copy = &sent[i];

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_EXT);
	zassert_equal(copy->req.dst_ext, CIE_EUI64);
	zassert_equal(copy->req.dst_ep, CIE_SRC_EP);
	zassert_equal(copy->req.src_ep, ZONE_EP);
	zassert_equal(copy->req.cluster, ZZB_ZCL_IAS_ZONE);
	zassert_equal(copy->len, 3 + len);
	zassert_equal(copy->data[0], 0x19);
	zassert_equal(copy->data[2], id);
	zassert_mem_equal(copy->data + 3, payload, len);
}

static void check_enrollment(uint8_t state, uint8_t zone)
{
	zassert_equal(zone80.zone_state, state);
	zassert_equal(zone80.zone_id, zone);
}

/* Writes the address of the CIE and enrolls endpoint 80 as zone 5, then resets the fakes */
static void enroll(void)
{
	write_cie(CIE_EUI64, CIE_SRC_EP);
	zassert_equal(enroll_response(0x00, 5), ZZB_ZCL_SUCCESS);
	reset_fakes();
}

/* Sets ZoneStatus of endpoint 80 as the application does */
static void set_status(uint16_t status)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), ZONE_EP, ZZB_ZCL_IAS_ZONE, ZZB_ZCL_IAS_ZONE_STATUS,
				    &status));
}

static bool timer_armed(void)
{
	return k_work_delayable_is_pending(&test_node()->zcl.ias_zone_timer);
}

/* Writes CurrentZoneSensitivityLevel of endpoint 80 and returns the status of the response */
static uint8_t write_level(uint8_t level)
{
	const uint8_t req[] = {0x00, ++tsn, 0x02, 0x13, 0x00, 0x20, level};
	int i = zzb_apsde_data_req_fake.call_count;

	receive_ep(ZONE_EP, ZZB_ZCL_IAS_ZONE, req, sizeof(req));
	zassert_mem_equal(sent[i].data, ((uint8_t[]){0x18, tsn, 0x04}), 3);
	return sent[i].data[3];
}

static uint8_t test_mode(uint8_t duration, uint8_t level)
{
	return command(ZZB_ZCL_IAS_ZONE_INITIATE_TEST_MODE, (uint8_t[]){duration, level}, 2);
}

/*
 * ZCL 8.2.2.1.3: the write of IAS_CIE_Address starts auto-enroll-request to the source endpoint of
 * the write, and Zone Enroll Response with Success enrolls the zone
 */
ZTEST(zzb_zcl_ias_zone, test_enroll_request)
{
	static const uint16_t written[] = {ZZB_ZCL_IAS_ZONE_CIE_ADDRESS};
	static const uint16_t enrolled[] = {ZZB_ZCL_IAS_ZONE_STATE, ZZB_ZCL_IAS_ZONE_ID};
	uint8_t next = test_node()->zcl.tsn;

	check_enrollment(0x00, 0xff);
	write_cie(CIE_EUI64, CIE_SRC_EP);
	zassert_equal(zone80.ias_cie_address, CIE_EUI64);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, written, ARRAY_SIZE(written));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	check_zone_cmd(1, ZZB_ZCL_IAS_ZONE_ENROLL_REQ, (uint8_t[]){0x15, 0x00, 0x34, 0x12}, 4);
	zassert_equal(sent[1].data[1], next);
	check_enrollment(0x00, 0xff);

	reset_fakes();
	zassert_equal(enroll_response(0x00, 5), ZZB_ZCL_SUCCESS);
	check_enrollment(0x01, 5);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, enrolled, ARRAY_SIZE(enrolled));
}

/* ZHA writes the stored address again at every rejoin; a new address ends the enrollment */
ZTEST(zzb_zcl_ias_zone, test_new_address)
{
	static const uint16_t ended[] = {ZZB_ZCL_IAS_ZONE_STATE, ZZB_ZCL_IAS_ZONE_ID,
					 ZZB_ZCL_IAS_ZONE_CIE_ADDRESS};

	enroll();
	write_cie(CIE_EUI64, 0x0c);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(zone80.cie_ep, CIE_SRC_EP);
	check_enrollment(0x01, 5);
	zassert_equal(zcl_cb_calls.count, 0);

	write_cie(0x0011223344556688, 0x0c);
	check_enrollment(0x00, 0xff);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, ended, ARRAY_SIZE(ended));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	zassert_equal(sent[2].req.dst_ext, 0x0011223344556688);
	zassert_equal(sent[2].req.dst_ep, 0x0c);
	zassert_equal(sent[2].data[2], ZZB_ZCL_IAS_ZONE_ENROLL_REQ);
}

/* An address of 0 or all ones ends the enrollment and sends no Zone Enroll Request */
ZTEST(zzb_zcl_ias_zone, test_no_address)
{
	enroll();
	write_cie(UINT64_MAX, CIE_SRC_EP);
	check_enrollment(0x00, 0xff);
	write_cie(0, CIE_SRC_EP);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* ZCL 8.2.2.2.1: another code ends the enrollment; an unsolicited response is accepted */
ZTEST(zzb_zcl_ias_zone, test_enroll_response)
{
	write_cie(CIE_EUI64, CIE_SRC_EP);
	zassert_equal(enroll_response(0x00, 3), ZZB_ZCL_SUCCESS);
	check_enrollment(0x01, 3);
	zassert_equal(enroll_response(0x02, 4), ZZB_ZCL_SUCCESS);
	check_enrollment(0x00, 0xff);
	zassert_equal(enroll_response(0x00, 0), ZZB_ZCL_SUCCESS);
	check_enrollment(0x01, 0);
}

/* ZCL 8.2.2.1.3: commands come only from the network address of IAS_CIE_Address */
ZTEST(zzb_zcl_ias_zone, test_not_authorized)
{
	const struct zzb_apsde_ind other = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ZONE_EP,
		.src_addr = 0x1234,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_IAS_ZONE,
	};

	zassert_equal(enroll_response(0x00, 3), ZZB_ZCL_NOT_AUTHORIZED);

	write_cie(CIE_EUI64, CIE_SRC_EP);
	reset_fakes();
	receive_ind(&other, (uint8_t[]){0x01, 0x60, 0x00, 0x00, 0x03}, 5);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x18, 0x60, 0x0b, 0x00, 0x7e}), 5);
	zassert_equal(zzb_aps_resolve_ext_fake.arg1_val, CIE_EUI64);

	zzb_aps_resolve_ext_fake.custom_fake = NULL;
	zzb_aps_resolve_ext_fake.return_val = -ENOENT;
	zassert_equal(enroll_response(0x00, 3), ZZB_ZCL_NOT_AUTHORIZED);
	check_enrollment(0x00, 0xff);

	reset_fakes();
	zassert_equal(command(ZZB_ZCL_IAS_ZONE_ENROLL_RSP, (uint8_t[]){0x00}, 1),
		      ZZB_ZCL_MALFORMED_COMMAND);
	write_cie(UINT64_MAX, CIE_SRC_EP);
	zassert_equal(enroll_response(0x00, 3), ZZB_ZCL_NOT_AUTHORIZED);
}

/* ZCL 8.2.2.3.1: a change of ZoneStatus of an enrolled zone */
ZTEST(zzb_zcl_ias_zone, test_notification)
{
	enroll();
	set_status(ZZB_ZCL_IAS_ZONE_STATUS_ALARM1 | ZZB_ZCL_IAS_ZONE_STATUS_TAMPER);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_zone_cmd(0, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x05, 0x00, 0x00, 0x05, 0x00, 0x00}, 6);
	zassert_false(timer_armed());

	set_status(0x0005);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* ZCL 8.2.2.1.3: trip-to-pair before enrollment; nothing without the address of a CIE */
ZTEST(zzb_zcl_ias_zone, test_trip_to_pair)
{
	set_status(0x0001);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	write_cie(CIE_EUI64, CIE_SRC_EP);
	reset_fakes();
	set_status(0x0000);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_zone_cmd(0, ZZB_ZCL_IAS_ZONE_ENROLL_REQ, (uint8_t[]){0x15, 0x00, 0x34, 0x12}, 4);
}

/*
 * A notification refused off a network or during a rejoin waits and is tried again after 5 s with
 * the ZoneStatus of that time; its delay counts from the first change not sent (ZCL 8.2.2.3.1.5).
 * Another error, such as no buffer, counts as sent.
 */
ZTEST(zzb_zcl_ias_zone, test_notification_retry)
{
	int (*take)(struct zzb_node *, struct net_buf *, const struct zzb_apsde_req *) =
		zzb_apsde_data_req_fake.custom_fake;

	enroll();
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOBUFS;
	set_status(0x0004);
	zassert_false(timer_armed());

	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	set_status(0x0001);
	zassert_true(timer_armed());
	k_sleep(K_SECONDS(2));
	zzb_apsde_data_req_fake.return_val = -EBUSY;
	set_status(0x0003);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);

	zzb_apsde_data_req_fake.custom_fake = take;
	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	/* 7 s in quarter seconds */
	check_zone_cmd(3, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x03, 0x00, 0x00, 0x05, 0x1c, 0x00}, 6);
	zassert_false(timer_armed());
}

/*
 * ZCL 8.2.2.1.2: NumberOfZoneSensitivityLevelsSupported reads as 2 while the application leaves it
 * 0 and limits CurrentZoneSensitivityLevel, which is saved
 */
ZTEST(zzb_zcl_ias_zone, test_sensitivity_level)
{
	static const uint16_t level[] = {ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL};

	zassert_equal(zone80.number_of_zone_sensitivity_levels_supported, 2);
	zassert_equal(write_level(3), ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_level(2), ZZB_ZCL_SUCCESS);
	zassert_ok(reboot());
	zassert_equal(zone80.current_zone_sensitivity_level, 2);

	reset_fakes();
	reset_node();
	zassert_equal(zone80.current_zone_sensitivity_level, 0);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, level, ARRAY_SIZE(level));
}

/*
 * ZCL 8.2.2.2.2: Initiate Test Mode sets the sensitivity level and bit 8 of ZoneStatus until the
 * end of its duration; the stack keeps bit 8 in a value of the application
 */
ZTEST(zzb_zcl_ias_zone, test_test_mode)
{
	static const uint16_t started[] = {ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL,
					   ZZB_ZCL_IAS_ZONE_STATUS};
	static const uint16_t ended[] = {ZZB_ZCL_IAS_ZONE_STATUS};

	enroll();
	zassert_equal(test_mode(3, 1), ZZB_ZCL_SUCCESS);
	zassert_equal(zone80.current_zone_sensitivity_level, 1);
	zassert_equal(zone80.zone_status, ZZB_ZCL_IAS_ZONE_STATUS_TEST);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, started, ARRAY_SIZE(started));
	check_zone_cmd(0, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x00, 0x01, 0x00, 0x05, 0x00, 0x00}, 6);

	set_status(ZZB_ZCL_IAS_ZONE_STATUS_ALARM1);
	check_zone_cmd(2, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x01, 0x01, 0x00, 0x05, 0x00, 0x00}, 6);

	reset_fakes();
	k_sleep(K_MSEC(2900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zone80.zone_status, ZZB_ZCL_IAS_ZONE_STATUS_ALARM1);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, ended, ARRAY_SIZE(ended));
	check_zone_cmd(0, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x01, 0x00, 0x00, 0x05, 0x00, 0x00}, 6);
	zassert_false(timer_armed());
}

/*
 * Initiate Normal Operation Mode ends the test mode, and so does a duration of 0 at once. A level
 * above NumberOfZoneSensitivityLevelsSupported is refused.
 */
ZTEST(zzb_zcl_ias_zone, test_normal_mode)
{
	static const uint16_t zero[] = {ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL,
					ZZB_ZCL_IAS_ZONE_STATUS, ZZB_ZCL_IAS_ZONE_STATUS};

	enroll();
	zassert_equal(test_mode(60, 0), ZZB_ZCL_SUCCESS);
	zassert_equal(command(ZZB_ZCL_IAS_ZONE_INITIATE_NORMAL_MODE, NULL, 0), ZZB_ZCL_SUCCESS);
	zassert_equal(zone80.zone_status, 0);
	check_zone_cmd(2, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x00, 0x00, 0x00, 0x05, 0x00, 0x00}, 6);
	zassert_false(timer_armed());

	reset_fakes();
	zassert_equal(test_mode(0, 2), ZZB_ZCL_SUCCESS);
	zassert_equal(zone80.zone_status, 0);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, zero, ARRAY_SIZE(zero));
	check_zone_cmd(0, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x00, 0x01, 0x00, 0x05, 0x00, 0x00}, 6);
	check_zone_cmd(1, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION,
		       (uint8_t[]){0x00, 0x00, 0x00, 0x05, 0x00, 0x00}, 6);
	zassert_false(timer_armed());

	zassert_equal(test_mode(10, 3), ZZB_ZCL_INVALID_FIELD);
	zassert_equal(zone80.current_zone_sensitivity_level, 2);
	set_status(ZZB_ZCL_IAS_ZONE_STATUS_TEST);
	zassert_equal(zone80.zone_status, 0);
}

/* The reset of the node ends the test mode */
ZTEST(zzb_zcl_ias_zone, test_reset_test_mode)
{
	enroll();
	zassert_equal(test_mode(60, 1), ZZB_ZCL_SUCCESS);
	zassert_true(timer_armed());
	reset_node();
	zassert_equal(zone80.zone_status, 0);
	zassert_equal(zone80.current_zone_sensitivity_level, 0);
	zassert_false(timer_armed());
}

/*
 * The IAS Zone client of a CIE passes the commands of the zones to zcl_cmd_cb, which answers Zone
 * Enroll Request
 */
ZTEST(zzb_zcl_ias_zone, test_client)
{
	static const uint8_t request[] = {0x19, 0x61, 0x01, 0x15, 0x00, 0x34, 0x12};
	static const uint8_t response[] = {0x11, 0x61, 0x42, 0xaa};
	static const uint8_t notification[] = {0x19, 0x62, 0x00, 0x01, 0x00,
					       0x00, 0x05, 0x00, 0x00};

	zcl_cmd_responds = true;
	receive_ep(ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, request, sizeof(request));
	zassert_equal(zcl_cmd_calls.count, 1);
	zassert_false(zcl_cmd_calls.cmd[0].server);
	zassert_equal(zcl_cmd_calls.cmd[0].id, ZZB_ZCL_IAS_ZONE_ENROLL_REQ);
	zassert_equal(zcl_cmd_calls.cmd[0].len, 4);
	zassert_mem_equal(zcl_cmd_calls.payload[0], request + 3, 4);
	zassert_ok(zcl_cmd_respond_err);
	check_frame(0, ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, response, sizeof(response));

	zcl_cmd_responds = false;
	receive_ep(ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, notification, sizeof(notification));
	zassert_equal(zcl_cmd_calls.count, 2);
	zassert_equal(zcl_cmd_calls.cmd[1].id, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* Shorter payloads and another command of the client */
ZTEST(zzb_zcl_ias_zone, test_client_errors)
{
	receive_ep(ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE,
		   (uint8_t[]){0x09, 0x63, 0x01, 0x15, 0x00, 0x34}, 6);
	check_frame(0, ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, (uint8_t[]){0x10, 0x63, 0x0b, 0x01, 0x80},
		    5);
	receive_ep(ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE,
		   (uint8_t[]){0x09, 0x64, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00}, 8);
	check_frame(1, ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, (uint8_t[]){0x10, 0x64, 0x0b, 0x00, 0x80},
		    5);
	receive_ep(ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, (uint8_t[]){0x09, 0x65, 0x02}, 3);
	check_frame(2, ZONE_CLIENT_EP, ZZB_ZCL_IAS_ZONE, (uint8_t[]){0x10, 0x65, 0x0b, 0x02, 0x81},
		    5);
	zassert_equal(zcl_cmd_calls.count, 0);
}

/* Section 15: the enrollment and the endpoint of the CIE are saved, and the reset deletes them */
ZTEST(zzb_zcl_ias_zone, test_records)
{
	static const uint16_t reset[] = {ZZB_ZCL_IAS_ZONE_STATE, ZZB_ZCL_IAS_ZONE_ID,
					 ZZB_ZCL_IAS_ZONE_CIE_ADDRESS};

	enroll();
	zassert_ok(reboot());
	check_enrollment(0x01, 5);
	zassert_equal(zone80.ias_cie_address, CIE_EUI64);
	zassert_equal(zone80.cie_ep, CIE_SRC_EP);

	reset_node();
	check_enrollment(0x00, 0xff);
	zassert_equal(zone80.ias_cie_address, 0);
	check_zcl_cb(ZONE_EP, ZZB_ZCL_IAS_ZONE, reset, ARRAY_SIZE(reset));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	zassert_ok(reboot());
	check_enrollment(0x00, 0xff);
	zassert_equal(zone80.ias_cie_address, 0);
	zassert_equal(zone80.cie_ep, 0);
}

ZTEST(zzb_zcl_ias_zone, test_bad_records)
{
	uint8_t rec[] = {2, CIE_SRC_EP};

	zassert_ok(settings_save_one("zzb/0/zcl/ias/80", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);
	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/zcl/ias/80", rec, 1));
	zassert_equal(reboot(), -EINVAL);

	/* A record of an endpoint without an IAS Zone server, and of an unknown one */
	zassert_ok(settings_save_one("zzb/0/zcl/ias/70", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/ias/9", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/ias/80", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_equal(zone80.cie_ep, CIE_SRC_EP);

	zassert_ok(settings_delete("zzb/0/zcl/ias/70"));
	zassert_ok(settings_delete("zzb/0/zcl/ias/9"));
}

ZTEST_SUITE(zzb_zcl_ias_zone, NULL, setup, before, after, NULL);
