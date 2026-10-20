/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

/* The records of the saved attributes of the test cluster */
#define RECORD       "zzb/0/zcl/1/0f00/0002"
#define RECORD_I16   "zzb/0/zcl/1/0f00/0008"
#define RECORD_U24   "zzb/0/zcl/1/0f00/0009"
#define RECORD_MAP32 "zzb/0/zcl/1/0f00/000a"
#define RECORD_I8    "zzb/0/zcl/1/0f00/000c"
#define RECORD_U32   "zzb/0/zcl/1/0f00/000d"
#define RECORD_E16   "zzb/0/zcl/1/0f00/000e"
#define RECORD_EUI64 "zzb/0/zcl/1/0f00/000f"
#define RECORD_I24   "zzb/0/zcl/1/0f00/0010"
#define RECORD_U48   "zzb/0/zcl/1/0f00/0013"
#define RECORD_U56   "zzb/0/zcl/1/0f00/0014"

/* Every test starts without the record, since ztest orders tests by name */
static void before(void *fixture)
{
	reset_fakes();
	values1 = (struct test_values){0};
	zassert_ok(settings_delete(RECORD));
	zassert_ok(settings_delete(RECORD_I16));
	zassert_ok(settings_delete(RECORD_U24));
	zassert_ok(settings_delete(RECORD_MAP32));
	zassert_ok(settings_delete(RECORD_I8));
	zassert_ok(settings_delete(RECORD_U32));
	zassert_ok(settings_delete(RECORD_E16));
	zassert_ok(settings_delete(RECORD_EUI64));
	zassert_ok(settings_delete(RECORD_I24));
	zassert_ok(settings_delete("zzb/0/zcl/1/0f00/0011"));
	zassert_ok(settings_delete("zzb/0/zcl/1/0f00/0012"));
	zassert_ok(settings_delete(RECORD_U48));
	zassert_ok(settings_delete(RECORD_U56));
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* Section 15: a non-volatile attribute is saved at every change and loaded at boot */
ZTEST(zzb_zcl_attr, test_saved_attribute)
{
	static const uint8_t req[] = {0x00, 0x60, 0x02, 0x01, 0x00, 0x20,
				      0x34, 0x02, 0x00, 0x21, 0x78, 0x56};

	receive(TEST_CLUSTER, req, sizeof(req));
	values1 = (struct test_values){0};
	zassert_ok(reboot());
	zassert_equal(values1.u16, 0x5678);
	zassert_equal(values1.u8, 0);
}

/* Section 16: wide and signed values are written, saved in the length of their type and loaded */
ZTEST(zzb_zcl_attr, test_wide_types)
{
	static const uint8_t req[] = {0x00, 0x62, 0x02, 0x08, 0x00, 0x29, 0xfe, 0xff, 0x09, 0x00,
				      0x22, 0x56, 0x34, 0x12, 0x0a, 0x00, 0x1b, 0xef, 0xcd, 0xab,
				      0x89, 0x0c, 0x00, 0x28, 0xfd, 0x0d, 0x00, 0x23, 0x04, 0x03,
				      0x02, 0x01, 0x0e, 0x00, 0x31, 0x15, 0x00, 0x0f, 0x00, 0xf0,
				      0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
	static const uint8_t rsp[] = {0x18, 0x62, 0x04, 0x00};
	uint8_t rec[9];

	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(values1.i16, -2);
	zassert_equal(values1.u24, 0x123456);
	zassert_equal(values1.map32, 0x89abcdef);
	zassert_equal(values1.i8, -3);
	zassert_equal(values1.u32, 0x01020304);
	zassert_equal(values1.e16, 0x0015);
	zassert_equal(values1.eui64, 0x1122334455667788);
	zassert_equal(settings_load_one(RECORD_U24, rec, sizeof(rec)), 4);
	zassert_mem_equal(rec, ((uint8_t[]){0x01, 0x56, 0x34, 0x12}), 4);
	zassert_equal(settings_load_one(RECORD_I8, rec, sizeof(rec)), 2);
	zassert_mem_equal(rec, ((uint8_t[]){0x01, 0xfd}), 2);
	zassert_equal(settings_load_one(RECORD_E16, rec, sizeof(rec)), 3);
	zassert_mem_equal(rec, ((uint8_t[]){0x01, 0x15, 0x00}), 3);
	zassert_equal(settings_load_one(RECORD_EUI64, rec, sizeof(rec)), 9);
	zassert_mem_equal(rec, ((uint8_t[]){0x01, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}),
			  9);

	values1 = (struct test_values){0};
	zassert_ok(reboot());
	zassert_equal(values1.i16, -2);
	zassert_equal(values1.u24, 0x123456);
	zassert_equal(values1.map32, 0x89abcdef);
	zassert_equal(values1.i8, -3);
	zassert_equal(values1.u32, 0x01020304);
	zassert_equal(values1.e16, 0x0015);
	zassert_equal(values1.eui64, 0x1122334455667788);
}

/* Section 16: an int24 is sign-extended in its field; uint48 and uint56 have 64-bit fields */
ZTEST(zzb_zcl_attr, test_long_types)
{
	static const uint8_t req[] = {
		0x00, 0x64, 0x02, 0x10, 0x00, 0x2a, 0xfe, 0xff, 0xff, 0x11, 0x00, 0x2b, 0xfd, 0xff,
		0xff, 0xff, 0x12, 0x00, 0xe2, 0x78, 0x56, 0x34, 0x12, 0x13, 0x00, 0x25, 0x06, 0x05,
		0x04, 0x03, 0x02, 0x01, 0x14, 0x00, 0x26, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
	static const uint8_t read[] = {0x00, 0x65, 0x00, 0x10, 0x00, 0x13, 0x00, 0x14, 0x00};
	static const uint8_t rsp[] = {0x18, 0x65, 0x01, 0x10, 0x00, 0x00, 0x2a, 0xfe,
				      0xff, 0xff, 0x13, 0x00, 0x00, 0x25, 0x06, 0x05,
				      0x04, 0x03, 0x02, 0x01, 0x14, 0x00, 0x00, 0x26,
				      0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
	uint8_t rec[9];

	receive(TEST_CLUSTER, req, sizeof(req));
	zassert_equal(values1.i24, -2);
	zassert_equal(values1.i32, -3);
	zassert_equal(values1.utc, 0x12345678);
	zassert_equal(values1.u48, 0x010203040506);
	zassert_equal(values1.u56, 0x01020304050607);
	zassert_equal(settings_load_one(RECORD_I24, rec, sizeof(rec)), 4);
	zassert_mem_equal(rec, ((uint8_t[]){0x01, 0xfe, 0xff, 0xff}), 4);
	zassert_equal(settings_load_one(RECORD_U48, rec, sizeof(rec)), 7);
	zassert_equal(settings_load_one(RECORD_U56, rec, sizeof(rec)), 8);

	receive(TEST_CLUSTER, read, sizeof(read));
	check_frame(1, 1, TEST_CLUSTER, rsp, sizeof(rsp));

	values1 = (struct test_values){0};
	zassert_ok(reboot());
	zassert_equal(values1.i24, -2);
	zassert_equal(values1.i32, -3);
	zassert_equal(values1.utc, 0x12345678);
	zassert_equal(values1.u48, 0x010203040506);
	zassert_equal(values1.u56, 0x01020304050607);
}

/* zzb_zcl_attr_set() takes int32_t for int24 and uint64_t for uint48 and uint56 */
ZTEST(zzb_zcl_attr, test_attr_set_long_types)
{
	struct zzb_node *node = test_node();
	int32_t i24 = -0x800000;
	uint64_t u48 = 0xffffffffffff;
	uint64_t u56 = 0xffffffffffffff;

	zassert_ok(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0010, &i24));
	zassert_ok(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0013, &u48));
	zassert_ok(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0014, &u56));
	zassert_equal(values1.i24, -0x800000);
	zassert_equal(values1.u48, 0xffffffffffff);
	zassert_equal(values1.u56, 0xffffffffffffff);
	zassert_equal(hook_calls.count, 3);

	i24 = 0x800000;
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0010, &i24), -EINVAL);
	i24 = -0x800001;
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0010, &i24), -EINVAL);
	u48++;
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0013, &u48), -EINVAL);
	u56++;
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0014, &u56), -EINVAL);
	zassert_equal(hook_calls.count, 3);
}

/* The check hook gets the value struct of the instance on the endpoint */
ZTEST(zzb_zcl_attr, test_check_values)
{
	static const uint8_t req[] = {0x00, 0x63, 0x02, 0x03, 0x00, 0x30, 0x01};

	receive_ep(2, TEST_CLUSTER, req, sizeof(req));
	zassert_equal_ptr(check_values, &values2);
	receive_ep(1, TEST_CLUSTER, req, sizeof(req));
	zassert_equal_ptr(check_values, &values1);
}

/* Section 16: READ_ONLY from the check hook refuses a remote write, not zzb_zcl_attr_set() */
ZTEST(zzb_zcl_attr, test_check_read_only)
{
	static const uint8_t req[] = {0x00, 0x66, 0x02, 0x04, 0x00, 0x18, 0x0f};
	static const uint8_t rsp[] = {0x18, 0x66, 0x04, 0x88, 0x04, 0x00};
	uint8_t map8 = 0x0f;

	values1.map16 = BIT(0);
	receive(TEST_CLUSTER, req, sizeof(req));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
	zassert_equal(values1.map8, 0);

	zassert_ok(zzb_zcl_attr_set(test_node(), 1, TEST_CLUSTER, 0x0004, &map8));
	zassert_equal(values1.map8, 0x0f);
}

/* Section 15: a record with an unknown version or length fails the boot */
ZTEST(zzb_zcl_attr, test_bad_record)
{
	zassert_ok(settings_save_one(RECORD, (uint8_t[]){0x02, 0x78, 0x56}, 3));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_save_one(RECORD, (uint8_t[]){0x01, 0x78}, 2));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_delete(RECORD));
	zassert_ok(settings_save_one(RECORD_EUI64, (uint8_t[]){0x01, 0x04, 0x03, 0x02, 0x01}, 5));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_delete(RECORD_EUI64));
	zassert_ok(reboot());
}

/* Records of attributes that the device definition does not have are ignored */
ZTEST(zzb_zcl_attr, test_unknown_records)
{
	static const uint8_t rec[] = {0x01, 0x78, 0x56};

	zassert_ok(settings_save_one("zzb/0/zcl/9/0f00/0002", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/1/0300/0002", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/1/0f00/0100", rec, sizeof(rec)));
	zassert_ok(settings_save_one("zzb/0/zcl/1/0f00/0001", rec, sizeof(rec)));
	zassert_ok(reboot());
	zassert_equal(values1.u8, 0);
	zassert_equal(values1.u16, 0);
	zassert_equal(values2.u16, 0);
	zassert_ok(settings_delete("zzb/0/zcl/9/0f00/0002"));
	zassert_ok(settings_delete("zzb/0/zcl/1/0300/0002"));
	zassert_ok(settings_delete("zzb/0/zcl/1/0f00/0100"));
	zassert_ok(settings_delete("zzb/0/zcl/1/0f00/0001"));
}

/* Section 16: the reset sets the defaults as a remote change does, then deletes the records */
ZTEST(zzb_zcl_attr, test_reset)
{
	static const uint8_t req[] = {0x00, 0x61, 0x02, 0x01, 0x00, 0x20, 0x05, 0x02,
				      0x00, 0x21, 0x07, 0x00, 0x0f, 0x00, 0xf0, 0x08,
				      0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

	receive(TEST_CLUSTER, req, sizeof(req));
	reset_fakes();
	reset_node();
	zassert_equal(values1.u8, 0);
	zassert_equal(values1.u16, 0);
	zassert_equal(values1.eui64, 0);
	zassert_equal(zcl_cb_calls.count, 3);
	zassert_equal(zcl_cb_calls.attr[0], 0x0001);
	zassert_equal(zcl_cb_calls.attr[1], 0x0002);
	zassert_equal(zcl_cb_calls.attr[2], 0x000f);

	values1.u16 = 9;
	zassert_ok(reboot());
	zassert_equal(values1.u16, 9);
}

/* The application sets a value on the stack thread: saved, the changed hook, no zcl_cb */
ZTEST(zzb_zcl_attr, test_attr_set)
{
	struct zzb_node *node = test_node();
	uint16_t u16 = 0x1111;
	uint8_t u8 = 0;

	zassert_ok(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0002, &u16));
	zassert_equal(values1.u16, 0x1111);
	zassert_equal(hook_calls.count, 1);
	zassert_equal(hook_calls.attr[0], 0x0002);
	zassert_equal(zcl_cb_calls.count, 0);

	values1.u16 = 0;
	zassert_ok(reboot());
	zassert_equal(values1.u16, 0x1111);

	/* The same value changes nothing */
	zassert_ok(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0001, &u8));
	zassert_equal(hook_calls.count, 1);
}

ZTEST(zzb_zcl_attr, test_attr_set_errors)
{
	struct zzb_node *node = test_node();
	uint8_t big = TEST_E8_MAX + 1;
	uint8_t two = 2;
	uint16_t u16 = 1;
	uint32_t u24 = 0x1000000;
	uint64_t eui64 = 1;

	zassert_equal(zzb_zcl_attr_set(node, 9, TEST_CLUSTER, 0x0002, &u16), -ENOENT);
	zassert_equal(zzb_zcl_attr_set(node, 1, 0x0300, 0x0002, &u16), -ENOENT);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0100, &u16), -ENOENT);
	/* On/Off is a client on endpoint 1 */
	zassert_equal(zzb_zcl_attr_set(node, 1, ON_OFF, 0x0000, &two), -ENOENT);

	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0xfffd, &u16), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0005, &u16), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0000, &two), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0003, &big), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0009, &u24), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x000f, &eui64), -EINVAL);
	zassert_equal(hook_calls.count, 0);
}

ZTEST_SUITE(zzb_zcl_attr, NULL, setup, before, after, NULL);
