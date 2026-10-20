/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/on_off.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_node();
	reset_fakes();
	values1 = (struct test_values){0};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* The entry of attr of the test cluster on endpoint 1, or NULL */
static const struct zzb_zcl_report *entry(uint16_t attr)
{
	ARRAY_FOR_EACH_PTR(test_node()->zcl.reports, r) {
		if (r->in_use && r->ep == 1 && r->cluster == TEST_CLUSTER && r->attr == attr) {
			return r;
		}
	}

	return NULL;
}

static int entries(void)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(test_node()->zcl.reports, r) {
		count += r->in_use;
	}

	return count;
}

/*
 * ZCL 2.5.7, 2.5.8: a discrete attribute without a reportable change (bool, min 1, max 10) and an
 * analog one with it (uint16, min 0, max 0, change 5)
 */
ZTEST(zzb_zcl_report, test_configure_reporting)
{
	static const uint8_t req[] = {0x00, 0x50, 0x06, 0x00, 0x00, 0x00, 0x10,
				      0x01, 0x00, 0x0a, 0x00, 0x00, 0x02, 0x00,
				      0x21, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00};
	static const uint8_t rsp[] = {0x18, 0x50, 0x07, 0x00};
	const struct zzb_zcl_report *r;

	values1.u16 = 0x1234;
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));

	r = entry(0x0000);
	zassert_not_null(r);
	zassert_equal(r->min, 1);
	zassert_equal(r->max, 10);
	r = entry(0x0002);
	zassert_not_null(r);
	zassert_equal(r->min, 0);
	zassert_equal(r->max, 0);
	zassert_equal(r->change, 5);
	zassert_equal(r->last, 0x1234);

	/* A new configuration of an attribute replaces its entry */
	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(entries(), 2);
}

/*
 * ZCL 2.5.7.3: the checks in their order; only the failed records are listed. The records are an
 * unknown attribute, an attribute that is not reportable, the bool attribute as uint8 (whose record
 * has a reportable change), a maximum interval below the minimum, a record of reports received
 * with a timeout, and a valid record.
 */
ZTEST(zzb_zcl_report, test_configure_checks)
{
	static const uint8_t req[] = {
		0x00, 0x51, 0x06, 0x00, 0xff, 0x00, 0x10, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x03,
		0x00, 0x30, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0a,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x20, 0x0a, 0x00, 0x05, 0x00, 0x01, 0x01, 0x00,
		0x00, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x20, 0x05, 0x00, 0x0a, 0x00, 0x01};
	static const uint8_t rsp[] = {0x18, 0x51, 0x07, 0x86, 0x00, 0xff, 0x00, 0x8c,
				      0x00, 0x03, 0x00, 0x8d, 0x00, 0x00, 0x00, 0x87,
				      0x00, 0x01, 0x00, 0x8c, 0x01, 0x00, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(entries(), 1);
	zassert_not_null(entry(0x0001));
}

/* ZCL 2.5.7.1.6: a maximum interval of 0xffff, or 0 with a minimum of 0xffff, ends the reports */
ZTEST(zzb_zcl_report, test_configure_removes)
{
	uint8_t req[] = {0x00, 0x52, 0x06, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x0a, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(entries(), 1);
	req[9] = 0xff;
	req[10] = 0xff;
	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(entries(), 0);

	req[9] = 0x0a;
	req[10] = 0x00;
	receive(TEST_CLUSTER, req, sizeof(req));
	req[7] = 0xff;
	req[8] = 0xff;
	req[9] = 0x00;
	req[10] = 0x00;
	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(entries(), 0);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
}

/* ZCL defines no status for a full table */
ZTEST(zzb_zcl_report, test_configure_full_table)
{
	static const uint8_t req[] = {0x00, 0x53, 0x06, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x0a,
				      0x00, 0x00, 0x01, 0x00, 0x20, 0x01, 0x00, 0x0a, 0x00, 0x01,
				      0x00, 0x02, 0x00, 0x21, 0x01, 0x00, 0x0a, 0x00, 0x01, 0x00};
	static const uint8_t rsp[] = {0x18, 0x53, 0x07, 0x89, 0x00, 0x01,
				      0x00, 0x89, 0x00, 0x02, 0x00};

	receive_ep(2, TEST_CLUSTER, req, sizeof(req));
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(1, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(entries(), CONFIG_ZZB_ZCL_REPORT_COUNT);
}

/* A truncated record configures nothing */
ZTEST(zzb_zcl_report, test_configure_malformed)
{
	static const uint8_t req[] = {0x00, 0x54, 0x06, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x0a,
				      0x00, 0x00, 0x02, 0x00, 0x21, 0x01, 0x00, 0x0a, 0x00, 0x01};
	static const uint8_t rsp[] = {0x18, 0x54, 0x0b, 0x06, 0x80};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(entries(), 0);
}

/*
 * ZCL 2.5.9, 2.5.10: a record per request record; the reportable change only for an analog type.
 * The request asks for the bool and uint16 attributes, which are configured, the uint8 one, which
 * is not, an attribute that is not reportable, an unknown one and the bool one as received.
 */
ZTEST(zzb_zcl_report, test_read_reporting_configuration)
{
	static const uint8_t configure[] = {0x00, 0x56, 0x06, 0x00, 0x00, 0x00, 0x10,
					    0x01, 0x00, 0x0a, 0x00, 0x00, 0x02, 0x00,
					    0x21, 0x02, 0x00, 0x2c, 0x01, 0x10, 0x00};
	static const uint8_t req[] = {0x00, 0x57, 0x08, 0x00, 0x00, 0x00, 0x00,
				      0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03,
				      0x00, 0x00, 0xff, 0x00, 0x01, 0x00, 0x00};
	static const uint8_t rsp[] = {0x18, 0x57, 0x09, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00,
				      0x0a, 0x00, 0x00, 0x00, 0x02, 0x00, 0x21, 0x02, 0x00, 0x2c,
				      0x01, 0x10, 0x00, 0x8b, 0x00, 0x01, 0x00, 0x8c, 0x00, 0x03,
				      0x00, 0x86, 0x00, 0xff, 0x00, 0x8c, 0x01, 0x00, 0x00};

	receive(TEST_CLUSTER, configure, sizeof(configure));
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(1, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.5.10: only as many records as fit in the frame */
ZTEST(zzb_zcl_report, test_read_reporting_configuration_fits)
{
	uint8_t req[3 + 3 * 30] = {0x00, 0x58, 0x08};

	/* Unknown attributes give 4 octets each, 19 of which fit in a payload of 79 octets */
	for (int i = 0; i < 30; i++) {
		req[3 + 3 * i + 1] = 0x10 + i;
	}

	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(sent[0].len, 3 + 4 * 19);
	zassert_mem_equal(sent[0].data + 3 + 4 * 18, ((uint8_t[]){0x86, 0x00, 0x22, 0x00}), 4);
}

/* Configures attr of the test cluster on endpoint 1 and forgets the response */
static void configure(uint16_t attr, uint8_t type, uint16_t min, uint16_t max, uint16_t change)
{
	uint8_t req[13] = {0x00, 0x60, 0x06, 0x00};
	size_t len = type == ZZB_ZCL_BOOL ? 11 : (ZZB_ZCL_IS_BYTE(type) ? 12 : 13);

	sys_put_le16(attr, req + 4);
	req[6] = type;
	sys_put_le16(min, req + 7);
	sys_put_le16(max, req + 9);
	sys_put_le16(change, req + 11);
	receive(TEST_CLUSTER, req, len);
	zassert_equal(sent[zzb_apsde_data_req_fake.call_count - 1].data[3], ZZB_ZCL_SUCCESS);
	reset_fakes();
}

static void set(uint16_t attr, uint16_t value)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, attr, &value));
}

/* Checks that frame i is a Report Attributes of the test cluster to the bound destinations */
static void check_report(int i, const uint8_t *records, size_t len)
{
	const struct frame_copy *copy = &sent[i];

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(copy->req.src_ep, 1);
	zassert_equal(copy->req.profile, 0x0104);
	zassert_equal(copy->req.cluster, TEST_CLUSTER);
	zassert_equal(copy->req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(copy->handle, ZZB_ZCL_HANDLE);
	zassert_equal(copy->len, 3 + len);
	zassert_equal(copy->data[0], 0x18);
	zassert_equal(copy->data[2], 0x0a);
	zassert_mem_equal(copy->data + 3, records, len);
}

/* ZCL 2.5.11.2.2: a discrete attribute is reported at any change */
ZTEST(zzb_zcl_report, test_discrete_change)
{
	configure(0x0000, ZZB_ZCL_BOOL, 0, 0, 0);
	set(0x0000, true);
	k_sleep(K_MSEC(10));
	check_report(0, (uint8_t[]){0x00, 0x00, 0x10, 0x01}, 4);

	/* The TSN comes from the counter of the node */
	set(0x0000, false);
	k_sleep(K_MSEC(10));
	check_report(1, (uint8_t[]){0x00, 0x00, 0x10, 0x00}, 4);
	zassert_equal(sent[1].data[1], (uint8_t)(sent[0].data[1] + 1));

	/* A write of the same value is no change */
	set(0x0000, false);
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* ZCL 2.5.11.2.3: an analog attribute is reported when it moves by the reportable change */
ZTEST(zzb_zcl_report, test_analog_change)
{
	configure(0x0002, ZZB_ZCL_UINT16, 0, 0, 5);
	set(0x0002, 4);
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	set(0x0002, 5);
	k_sleep(K_MSEC(10));
	check_report(0, (uint8_t[]){0x02, 0x00, 0x21, 0x05, 0x00}, 5);

	/* Measured from the value of the last report */
	set(0x0002, 1);
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	set(0x0002, 0);
	k_sleep(K_MSEC(10));
	check_report(1, (uint8_t[]){0x02, 0x00, 0x21, 0x00, 0x00}, 5);
}

/* The reportable change of an int16 is measured with its sign */
ZTEST(zzb_zcl_report, test_signed_change)
{
	values1.i16 = -2;
	configure(0x0008, ZZB_ZCL_INT16, 0, 0, 5);
	set(0x0008, 2);
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	set(0x0008, 3);
	k_sleep(K_MSEC(10));
	check_report(0, (uint8_t[]){0x08, 0x00, 0x29, 0x03, 0x00}, 5);
}

/* The reportable change of an int8 is measured with its sign */
ZTEST(zzb_zcl_report, test_signed_change_int8)
{
	int8_t i8 = 2;

	values1.i8 = -2;
	configure(0x000c, ZZB_ZCL_INT8, 0, 0, 5);
	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x000c, &i8));
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	i8 = 3;
	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x000c, &i8));
	k_sleep(K_MSEC(10));
	check_report(0, (uint8_t[]){0x0c, 0x00, 0x28, 0x03}, 4);
}

/* ZHA 2.3.1 configures CurrentSummationDelivered (uint48) 30/900/1; the change has 48 bits */
ZTEST(zzb_zcl_report, test_uint48_change)
{
	static const uint8_t req[] = {0x00, 0x60, 0x06, 0x00, 0x13, 0x00, 0x25, 0x1e, 0x00,
				      0x84, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint64_t u48 = 0x0200000000;

	values1.u48 = 0x0100000000;
	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(sent[0].data[3], ZZB_ZCL_SUCCESS);
	reset_fakes();

	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x0013, &u48));
	k_sleep(K_SECONDS(30));
	check_report(0, (uint8_t[]){0x13, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00}, 9);
}

/* The reportable change of an int24, such as InstantaneousDemand, is measured with its sign */
ZTEST(zzb_zcl_report, test_int24_change)
{
	static const uint8_t req[] = {0x00, 0x60, 0x06, 0x00, 0x10, 0x00, 0x2a,
				      0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00};
	int32_t i24 = 2;

	values1.i24 = -2;
	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(sent[0].data[3], ZZB_ZCL_SUCCESS);
	reset_fakes();

	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x0010, &i24));
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	i24 = 3;
	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x0010, &i24));
	k_sleep(K_MSEC(10));
	check_report(0, (uint8_t[]){0x10, 0x00, 0x2a, 0x03, 0x00, 0x00}, 6);
}

/* Section 16: a reportable change above 16 bits is kept as 0xffff */
ZTEST(zzb_zcl_report, test_wide_change)
{
	static const uint8_t configure[] = {0x00, 0x61, 0x06, 0x00, 0x13, 0x00, 0x25, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
	static const uint8_t req[] = {0x00, 0x62, 0x08, 0x00, 0x13, 0x00};
	static const uint8_t rsp[] = {0x18, 0x62, 0x09, 0x00, 0x00, 0x13, 0x00, 0x25, 0x00,
				      0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};

	receive(TEST_CLUSTER, configure, sizeof(configure));
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(1, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* A change within the minimum interval after the last report waits for its end */
ZTEST(zzb_zcl_report, test_minimum_interval)
{
	configure(0x0000, ZZB_ZCL_BOOL, 2, 0, 0);
	set(0x0000, true);
	k_sleep(K_MSEC(1900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	check_report(0, (uint8_t[]){0x00, 0x00, 0x10, 0x01}, 4);

	k_sleep(K_MSEC(2500));
	set(0x0000, false);
	k_sleep(K_MSEC(10));
	check_report(1, (uint8_t[]){0x00, 0x00, 0x10, 0x00}, 4);
}

/* ZCL 2.5.11.2.1: a report follows the maximum interval after the last one */
ZTEST(zzb_zcl_report, test_maximum_interval)
{
	values1.b = true;
	configure(0x0000, ZZB_ZCL_BOOL, 0, 3, 0);
	k_sleep(K_MSEC(2900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	check_report(0, (uint8_t[]){0x00, 0x00, 0x10, 0x01}, 4);
	k_sleep(K_MSEC(3000));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

/* ZCL 2.5.11.2.5: attributes of one cluster due at the same time go in one frame */
ZTEST(zzb_zcl_report, test_one_frame)
{
	values1.u8 = 7;
	configure(0x0000, ZZB_ZCL_BOOL, 0, 2, 0);
	configure(0x0001, ZZB_ZCL_UINT8, 0, 2, 1);
	k_sleep(K_MSEC(2100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_report(0, (uint8_t[]){0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x20, 0x07}, 8);
}

/*
 * Without a binding the report counts as sent. Off a network or during a rejoin it is sent again
 * after 5 s.
 */
ZTEST(zzb_zcl_report, test_send_errors)
{
	configure(0x0000, ZZB_ZCL_BOOL, 0, 0, 0);
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOENT;
	set(0x0000, true);
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);

	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	set(0x0000, false);
	k_sleep(K_MSEC(100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	k_sleep(K_MSEC(5000));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	reset_fakes();
	k_sleep(K_MSEC(5000));
	check_report(0, (uint8_t[]){0x00, 0x00, 0x10, 0x00}, 4);
}

/* No timer runs without a configuration that needs one */
ZTEST(zzb_zcl_report, test_timer)
{
	set(0x0000, true);
	zassert_false(k_work_delayable_is_pending(&test_node()->zcl.report_timer));
	configure(0x0000, ZZB_ZCL_BOOL, 0, 0, 0);
	zassert_false(k_work_delayable_is_pending(&test_node()->zcl.report_timer));
	configure(0x0000, ZZB_ZCL_BOOL, 0, 10, 0);
	zassert_true(k_work_delayable_is_pending(&test_node()->zcl.report_timer));
	reset_node();
	zassert_false(k_work_delayable_is_pending(&test_node()->zcl.report_timer));
}

/* The reset of the node clears the table */
ZTEST(zzb_zcl_report, test_reset)
{
	static const uint8_t req[] = {0x00, 0x55, 0x06, 0x00, 0x00, 0x00,
				      0x10, 0x01, 0x00, 0x0a, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	reset_node();
	zassert_equal(entries(), 0);
}

/* ZCL 2.3.5: the configuration survives a reboot, and the reboot sends no report */
ZTEST(zzb_zcl_report, test_saved_configuration)
{
	const struct zzb_zcl_report *r;

	configure(0x0000, ZZB_ZCL_BOOL, 1, 10, 0);
	configure(0x0002, ZZB_ZCL_UINT16, 0, 300, 16);
	zassert_ok(reboot());
	k_sleep(K_MSEC(100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	r = entry(0x0000);
	zassert_not_null(r);
	zassert_equal(r->min, 1);
	zassert_equal(r->max, 10);
	r = entry(0x0002);
	zassert_not_null(r);
	zassert_equal(r->max, 300);
	zassert_equal(r->change, 16);

	/* A removed configuration stays removed */
	configure(0x0000, ZZB_ZCL_BOOL, 0, 0xffff, 0);
	zassert_ok(reboot());
	zassert_is_null(entry(0x0000));
	zassert_not_null(entry(0x0002));

	reset_node();
	zassert_ok(reboot());
	zassert_equal(entries(), 0);
}

/* A change of OnOff by StartUpOnOff at boot is reported: ZHA does not read OnOff again */
ZTEST(zzb_zcl_report, test_start_up_change_is_reported)
{
	static const uint8_t req[] = {0x00, 0x61, 0x06, 0x00, 0x00, 0x00,
				      0x10, 0x00, 0x00, 0x84, 0x03};
	uint8_t start_up = ZZB_ZCL_START_UP_TOGGLE;

	receive_ep(2, ZZB_ZCL_ON_OFF, req, sizeof(req));
	zassert_ok(zzb_zcl_attr_set(test_node(), 2, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_START_UP,
				    &start_up));
	reset_fakes();
	zassert_ok(reboot());
	k_sleep(K_MSEC(10));

	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(sent[0].req.src_ep, 2);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_ON_OFF);
	zassert_mem_equal(sent[0].data + 2, ((uint8_t[]){0x0a, 0x00, 0x00, 0x10, 0x01}), 5);
}

ZTEST(zzb_zcl_report, test_bad_records)
{
	uint8_t rec[12] = {2, 1, 0x00, 0x0f, 0x00, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x00, 0x00};

	zassert_ok(settings_save_one("zzb/0/zcl/rep/0", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/zcl/rep/0", rec, sizeof(rec) - 1));
	zassert_equal(reboot(), -EINVAL);

	/* A record of an endpoint that the device definition does not have is ignored */
	rec[1] = 9;
	zassert_ok(settings_save_one("zzb/0/zcl/rep/0", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_equal(entries(), 0);
	zassert_ok(settings_delete("zzb/0/zcl/rep/0"));
}

ZTEST_SUITE(zzb_zcl_report, NULL, setup, before, after, NULL);
