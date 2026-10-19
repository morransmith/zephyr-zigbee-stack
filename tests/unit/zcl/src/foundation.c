/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "fakes.h"

#define READ_ATTRIBUTES 0x00

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	static const uint8_t octstr[] = {0x02, 0xaa, 0xbb};

	reset_fakes();
	values1 = (struct test_values){
		.b = true,
		.u8 = 0x12,
		.u16 = 0x3456,
		.e8 = 0x02,
		.map8 = 0x81,
		.str = "zzb",
		.octstr = octstr,
		.i16 = -2,
		.u24 = 0x123456,
		.map32 = 0x89abcdef,
		.map16 = 0x0018,
	};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/*
 * ZCL 2.5.2: every storage type, ClusterRevision TEST_REVISION, an empty string and an unknown
 * attribute
 */
ZTEST(zzb_zcl_foundation, test_read_attributes)
{
	/* Attributes 0x0000 to 0x0007, 0xfffd and 0x0100 */
	static const uint8_t req[] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02,
				      0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06,
				      0x00, 0x07, 0x00, 0xfd, 0xff, 0x00, 0x01};
	static const uint8_t rsp[] = {
		0x18, 0x40, 0x01, 0x00, 0x00, 0x00, 0x10, 0x01, 0x01, 0x00, 0x00, 0x20,
		0x12, 0x02, 0x00, 0x00, 0x21, 0x56, 0x34, 0x03, 0x00, 0x00, 0x30, 0x02,
		0x04, 0x00, 0x00, 0x18, 0x81, 0x05, 0x00, 0x00, 0x42, 0x03, 'z',  'z',
		'b',  0x06, 0x00, 0x00, 0x41, 0x02, 0xaa, 0xbb, 0x07, 0x00, 0x00, 0x42,
		0x00, 0xfd, 0xff, 0x00, 0x21, 0x01, 0x00, 0x00, 0x01, 0x86,
	};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.6.2: int16, uint24, map32 and map16 in the length of their type */
ZTEST(zzb_zcl_foundation, test_read_wide_types)
{
	static const uint8_t req[] = {0x00, 0x49, 0x00, 0x08, 0x00, 0x09,
				      0x00, 0x0a, 0x00, 0x0b, 0x00};
	static const uint8_t rsp[] = {0x18, 0x49, 0x01, 0x08, 0x00, 0x00, 0x29, 0xfe, 0xff, 0x09,
				      0x00, 0x00, 0x22, 0x56, 0x34, 0x12, 0x0a, 0x00, 0x00, 0x1b,
				      0xef, 0xcd, 0xab, 0x89, 0x0b, 0x00, 0x00, 0x19, 0x18, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.3.2: an odd last octet is ignored; a broadcast gets the response too */
ZTEST(zzb_zcl_foundation, test_read_broadcast)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = 0xfffd,
		.dst_ep = 1,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = TEST_CLUSTER,
	};
	static const uint8_t req[] = {0x00, 0x41, READ_ATTRIBUTES, 0x01, 0x00, 0x02};
	static const uint8_t rsp[] = {0x18, 0x41, 0x01, 0x01, 0x00, 0x00, 0x20, 0x12};

	receive_ind(&ind, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.5.1.3: a value that does not fit gets INSUFFICIENT_SPACE, and a shorter one still fits */
ZTEST(zzb_zcl_foundation, test_read_insufficient_space)
{
	static const uint8_t req[] = {0x00, 0x42, READ_ATTRIBUTES, 0x00, 0x00, 0x05, 0x00,
				      0x01, 0x00};
	static const uint8_t rsp[] = {0x18, 0x42, 0x01, 0x00, 0x00, 0x00, 0x10, 0x01,
				      0x05, 0x00, 0x89, 0x01, 0x00, 0x00, 0x20, 0x12};
	/* The bool record leaves 74 octets of the 79, and the string record needs 79 */
	char str[75];

	memset(str, 'a', sizeof(str) - 1);
	str[sizeof(str) - 1] = '\0';
	values1.str = str;
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.5.1.3: the response ends where a status record does not fit */
ZTEST(zzb_zcl_foundation, test_read_stops)
{
	uint8_t req[3 + 2 * 27] = {0x00, 0x43, READ_ATTRIBUTES};
	uint8_t rsp[3 + 3 * 26] = {0x18, 0x43, 0x01};

	for (int i = 0; i < 27; i++) {
		sys_put_le16(0x1000 + i, req + 3 + 2 * i);
	}

	for (int i = 0; i < 26; i++) {
		sys_put_le16(0x1000 + i, rsp + 3 + 3 * i);
		rsp[5 + 3 * i] = 0x86;
	}

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.5.14: attribute IDs and data types in ascending order, then discovery complete */
ZTEST(zzb_zcl_foundation, test_discover_attributes)
{
	static const uint8_t all[] = {0x18, 0x44, 0x0d, 0x01, 0x00, 0x00, 0x10, 0x01, 0x00,
				      0x20, 0x02, 0x00, 0x21, 0x03, 0x00, 0x30, 0x04, 0x00,
				      0x18, 0x05, 0x00, 0x42, 0x06, 0x00, 0x41, 0x07, 0x00,
				      0x42, 0x08, 0x00, 0x29, 0x09, 0x00, 0x22, 0x0a, 0x00,
				      0x1b, 0x0b, 0x00, 0x19, 0xfd, 0xff, 0x21};
	static const uint8_t more[] = {0x18, 0x45, 0x0d, 0x00, 0x03, 0x00, 0x30, 0x04, 0x00, 0x18};
	static const uint8_t last[] = {0x18, 0x46, 0x0d, 0x01, 0x0b, 0x00, 0x19, 0xfd, 0xff, 0x21};
	static const uint8_t none[] = {0x18, 0x47, 0x0d, 0x01};
	static const uint8_t zero[] = {0x18, 0x48, 0x0d, 0x00};

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x44, 0x0c, 0x00, 0x00, 0x10}, 6);
	check_frame(0, 1, TEST_CLUSTER, all, sizeof(all));

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x45, 0x0c, 0x03, 0x00, 0x02}, 6);
	check_frame(1, 1, TEST_CLUSTER, more, sizeof(more));

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x46, 0x0c, 0x0b, 0x00, 0x02}, 6);
	check_frame(2, 1, TEST_CLUSTER, last, sizeof(last));

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x47, 0x0c, 0xfe, 0xff, 0x05}, 6);
	check_frame(3, 1, TEST_CLUSTER, none, sizeof(none));

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x48, 0x0c, 0x00, 0x00, 0x00}, 6);
	check_frame(4, 1, TEST_CLUSTER, zero, sizeof(zero));
}

/* ZCL Table 2-12: a field is missing */
ZTEST(zzb_zcl_foundation, test_discover_malformed)
{
	static const uint8_t rsp[] = {0x18, 0x49, 0x0b, 0x0c, 0x80};

	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x49, 0x0c, 0x00, 0x00}, 5);
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

static void check_notified(int i, uint16_t attr)
{
	zassert_true(hook_calls.count > i);
	zassert_equal(hook_calls.ep[i], 1);
	zassert_equal(hook_calls.attr[i], attr);
	zassert_true(zcl_cb_calls.count > i);
	zassert_equal(zcl_cb_calls.ep[i], 1);
	zassert_equal(zcl_cb_calls.cluster[i], TEST_CLUSTER);
	zassert_equal(zcl_cb_calls.attr[i], attr);
}

/* ZCL 2.5.5: one SUCCESS record when every attribute is written */
ZTEST(zzb_zcl_foundation, test_write_attributes)
{
	static const uint8_t req[] = {0x00, 0x50, 0x02, 0x01, 0x00, 0x20,
				      0x34, 0x02, 0x00, 0x21, 0x34, 0x12};
	static const uint8_t rsp[] = {0x18, 0x50, 0x04, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(values1.u8, 0x34);
	zassert_equal(values1.u16, 0x1234);
	zassert_equal(hook_calls.count, 2);
	zassert_equal(zcl_cb_calls.count, 2);
	check_notified(0, 0x0001);
	check_notified(1, 0x0002);
}

/* ZCL 2.5.3.3: the checks in order, a status record of status and ID per failed attribute */
ZTEST(zzb_zcl_foundation, test_write_checks)
{
	/*
	 * Records: an unsupported attribute, the uint8 written as uint16, the string and the
	 * constant, which are read-only, the bool and the enum8 out of range, then the map8
	 */
	static const uint8_t req[] = {0x00, 0x51, 0x02, 0x00, 0x01, 0x20, 0x00, 0x01, 0x00,
				      0x21, 0x00, 0x00, 0x05, 0x00, 0x42, 0x01, 'a',  0xfd,
				      0xff, 0x21, 0x02, 0x00, 0x00, 0x00, 0x10, 0x02, 0x03,
				      0x00, 0x30, 0x11, 0x04, 0x00, 0x18, 0x0f};
	static const uint8_t rsp[] = {0x18, 0x51, 0x04, 0x86, 0x00, 0x01, 0x8d,
				      0x01, 0x00, 0x88, 0x05, 0x00, 0x88, 0xfd,
				      0xff, 0x87, 0x00, 0x00, 0x87, 0x03, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_true(values1.b);
	zassert_equal(values1.e8, 0x02);
	zassert_equal(values1.map8, 0x0f);
	zassert_equal(zcl_cb_calls.count, 1);
	check_notified(0, 0x0004);
}

/* The application is notified only of a new value */
ZTEST(zzb_zcl_foundation, test_write_same_value)
{
	static const uint8_t req[] = {0x00, 0x52, 0x02, 0x01, 0x00, 0x20, 0x12};
	static const uint8_t rsp[] = {0x18, 0x52, 0x04, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(hook_calls.count, 0);
	zassert_equal(zcl_cb_calls.count, 0);
}

/* A record that cannot be parsed fails the command before anything is written */
ZTEST(zzb_zcl_foundation, test_write_malformed)
{
	static const uint8_t truncated[] = {0x00, 0x53, 0x02, 0x01, 0x00, 0x20,
					    0x34, 0x02, 0x00, 0x21, 0x34};
	static const uint8_t array[] = {0x00, 0x54, 0x02, 0x01, 0x00, 0x20, 0x34,
					0x02, 0x00, 0x48, 0x20, 0x00, 0x00};
	static const uint8_t short_record[] = {0x00, 0x55, 0x02, 0x01, 0x00, 0x20, 0x34, 0x02};

	receive(TEST_CLUSTER, truncated, sizeof(truncated));
	check_frame(0, 1, TEST_CLUSTER, (uint8_t[]){0x18, 0x53, 0x0b, 0x02, 0x80}, 5);
	receive(TEST_CLUSTER, array, sizeof(array));
	check_frame(1, 1, TEST_CLUSTER, (uint8_t[]){0x18, 0x54, 0x0b, 0x02, 0x80}, 5);
	receive(TEST_CLUSTER, short_record, sizeof(short_record));
	check_frame(2, 1, TEST_CLUSTER, (uint8_t[]){0x18, 0x55, 0x0b, 0x02, 0x80}, 5);
	zassert_equal(values1.u8, 0x12);
	zassert_equal(zcl_cb_calls.count, 0);
}

/* ZCL 2.5.4: one failed record leaves every attribute unchanged */
ZTEST(zzb_zcl_foundation, test_write_undivided)
{
	static const uint8_t failing[] = {0x00, 0x56, 0x03, 0x01, 0x00, 0x20,
					  0x40, 0x00, 0x01, 0x20, 0x00};
	static const uint8_t failed[] = {0x18, 0x56, 0x04, 0x86, 0x00, 0x01};
	static const uint8_t passing[] = {0x00, 0x57, 0x03, 0x01, 0x00, 0x20,
					  0x40, 0x02, 0x00, 0x21, 0x01, 0x01};
	static const uint8_t written[] = {0x18, 0x57, 0x04, 0x00};

	receive(TEST_CLUSTER, failing, sizeof(failing));
	check_frame(0, 1, TEST_CLUSTER, failed, sizeof(failed));
	zassert_equal(values1.u8, 0x12);
	zassert_equal(zcl_cb_calls.count, 0);

	receive(TEST_CLUSTER, passing, sizeof(passing));
	check_frame(1, 1, TEST_CLUSTER, written, sizeof(written));
	zassert_equal(values1.u8, 0x40);
	zassert_equal(values1.u16, 0x0101);
	zassert_equal(zcl_cb_calls.count, 2);
	check_notified(0, 0x0001);
	check_notified(1, 0x0002);
}

/* ZCL 2.5.6.3: no frame answers Write Attributes No Response, also in error */
ZTEST(zzb_zcl_foundation, test_write_no_response)
{
	static const uint8_t req[] = {0x00, 0x58, 0x05, 0x01, 0x00, 0x20,
				      0x41, 0x00, 0x01, 0x20, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(values1.u8, 0x41);
	zassert_equal(zcl_cb_calls.count, 1);
	check_notified(0, 0x0001);

	/* Malformed, an unsupported cluster, manufacturer specific */
	receive(TEST_CLUSTER, (uint8_t[]){0x00, 0x59, 0x05, 0x01, 0x00, 0x21, 0x00}, 7);
	receive(0x0300, req, sizeof(req));
	receive(TEST_CLUSTER, (uint8_t[]){0x04, 0x34, 0x12, 0x5a, 0x05, 0x01, 0x00, 0x20, 0x42}, 9);
	zassert_equal(values1.u8, 0x41);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

ZTEST_SUITE(zzb_zcl_foundation, NULL, setup, before, after, NULL);
