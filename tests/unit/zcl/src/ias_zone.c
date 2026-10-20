/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/ias_zone.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

/* The IEEE address of the CIE, which writes it from this endpoint */
#define CIE_EUI64 0x0011223344556677
#define CIE_EP    0x0b

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Each test starts without a CIE and without records */
static void before(void *fixture)
{
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
	zassert_equal(copy->req.dst_ep, CIE_EP);
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
	write_cie(CIE_EUI64, CIE_EP);
	zassert_equal(enroll_response(0x00, 5), ZZB_ZCL_SUCCESS);
	reset_fakes();
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
	write_cie(CIE_EUI64, CIE_EP);
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
	zassert_equal(zone80.cie_ep, CIE_EP);
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
	write_cie(UINT64_MAX, CIE_EP);
	check_enrollment(0x00, 0xff);
	write_cie(0, CIE_EP);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* ZCL 8.2.2.2.1: another code ends the enrollment; an unsolicited response is accepted */
ZTEST(zzb_zcl_ias_zone, test_enroll_response)
{
	write_cie(CIE_EUI64, CIE_EP);
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

	write_cie(CIE_EUI64, CIE_EP);
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
	write_cie(UINT64_MAX, CIE_EP);
	zassert_equal(enroll_response(0x00, 3), ZZB_ZCL_NOT_AUTHORIZED);
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
	zassert_equal(zone80.cie_ep, CIE_EP);

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
	uint8_t rec[] = {2, CIE_EP};

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
	zassert_equal(zone80.cie_ep, CIE_EP);

	zassert_ok(settings_delete("zzb/0/zcl/ias/70"));
	zassert_ok(settings_delete("zzb/0/zcl/ias/9"));
}

ZTEST_SUITE(zzb_zcl_ias_zone, NULL, setup, before, after, NULL);
