/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"

#define CONFIGURE_REPORTING 0x06

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
	static const uint8_t req[] = {0x00, 0x50, CONFIGURE_REPORTING,
				      0x00, 0x00, 0x00,
				      0x10, 0x01, 0x00,
				      0x0a, 0x00, 0x00,
				      0x02, 0x00, 0x21,
				      0x00, 0x00, 0x00,
				      0x00, 0x05, 0x00};
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
	uint8_t req[] = {0x00, 0x52, CONFIGURE_REPORTING, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00,
			 0x0a, 0x00};

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
	static const uint8_t req[] = {0x00, 0x53, CONFIGURE_REPORTING,
				      0x00, 0x00, 0x00,
				      0x10, 0x01, 0x00,
				      0x0a, 0x00, 0x00,
				      0x01, 0x00, 0x20,
				      0x01, 0x00, 0x0a,
				      0x00, 0x01, 0x00,
				      0x02, 0x00, 0x21,
				      0x01, 0x00, 0x0a,
				      0x00, 0x01, 0x00};
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
	static const uint8_t req[] = {0x00, 0x54, CONFIGURE_REPORTING,
				      0x00, 0x00, 0x00,
				      0x10, 0x01, 0x00,
				      0x0a, 0x00, 0x00,
				      0x02, 0x00, 0x21,
				      0x01, 0x00, 0x0a,
				      0x00, 0x01};
	static const uint8_t rsp[] = {0x18, 0x54, 0x0b, CONFIGURE_REPORTING, 0x80};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(entries(), 0);
}

/* The reset of the node clears the table */
ZTEST(zzb_zcl_report, test_reset)
{
	static const uint8_t req[] = {
		0x00, 0x55, CONFIGURE_REPORTING, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x0a, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	reset_node();
	zassert_equal(entries(), 0);
}

ZTEST_SUITE(zzb_zcl_report, NULL, setup, before, after, NULL);
