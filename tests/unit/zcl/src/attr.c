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

/* The record of the saved attribute of the test cluster */
#define RECORD "zzb/0/zcl/1/0f00/0002"

/* Every test starts without the record, since ztest orders tests by name */
static void before(void *fixture)
{
	reset_fakes();
	values1 = (struct test_values){0};
	zassert_ok(settings_delete(RECORD));
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

/* Section 15: a record with an unknown version or length fails the boot */
ZTEST(zzb_zcl_attr, test_bad_record)
{
	zassert_ok(settings_save_one(RECORD, (uint8_t[]){0x02, 0x78, 0x56}, 3));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_save_one(RECORD, (uint8_t[]){0x01, 0x78}, 2));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_delete(RECORD));
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
	static const uint8_t req[] = {0x00, 0x61, 0x02, 0x01, 0x00, 0x20,
				      0x05, 0x02, 0x00, 0x21, 0x07, 0x00};

	receive(TEST_CLUSTER, req, sizeof(req));
	reset_fakes();
	reset_node();
	zassert_equal(values1.u8, 0);
	zassert_equal(values1.u16, 0);
	zassert_equal(zcl_cb_calls.count, 2);
	zassert_equal(zcl_cb_calls.attr[0], 0x0001);
	zassert_equal(zcl_cb_calls.attr[1], 0x0002);

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

	zassert_equal(zzb_zcl_attr_set(node, 9, TEST_CLUSTER, 0x0002, &u16), -ENOENT);
	zassert_equal(zzb_zcl_attr_set(node, 1, 0x0300, 0x0002, &u16), -ENOENT);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0100, &u16), -ENOENT);
	/* On/Off is a client on endpoint 1 */
	zassert_equal(zzb_zcl_attr_set(node, 1, ON_OFF, 0x0000, &two), -ENOENT);

	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0xfffd, &u16), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0005, &u16), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0000, &two), -EINVAL);
	zassert_equal(zzb_zcl_attr_set(node, 1, TEST_CLUSTER, 0x0003, &big), -EINVAL);
	zassert_equal(hook_calls.count, 0);
}

ZTEST_SUITE(zzb_zcl_attr, NULL, setup, before, after, NULL);
