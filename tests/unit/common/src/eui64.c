/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "common/eui64.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(ssize_t, z_impl_hwinfo_get_device_id, uint8_t *, size_t);

static uint8_t id[8];
static size_t id_len;

static ssize_t device_id_fake(uint8_t *buffer, size_t length)
{
	size_t len = MIN(length, id_len);

	memcpy(buffer, id, len);
	return len;
}

static void set_id(const uint8_t *bytes, size_t len)
{
	memcpy(id, bytes, len);
	id_len = len;
	z_impl_hwinfo_get_device_id_fake.custom_fake = device_id_fake;
}

static void before(void *fixture)
{
	RESET_FAKE(z_impl_hwinfo_get_device_id);
}

ZTEST(zzb_eui64, test_8_byte_id_is_used_as_is)
{
	uint64_t eui64;

	set_id((const uint8_t[]){0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}, 8);

	zassert_ok(zzb_eui64_get(&eui64));
	zassert_equal(eui64, 0x0102030405060708ULL);
}

ZTEST(zzb_eui64, test_6_byte_id_gets_fffe_after_oui)
{
	uint64_t eui64;

	set_id((const uint8_t[]){0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}, 6);

	zassert_ok(zzb_eui64_get(&eui64));
	zassert_equal(eui64, 0xaabbccfffeddeeffULL);
}

ZTEST(zzb_eui64, test_short_id_is_zero_padded)
{
	uint64_t eui64;

	set_id((const uint8_t[]){0x11, 0x22, 0x33, 0x44}, 4);

	zassert_ok(zzb_eui64_get(&eui64));
	zassert_equal(eui64, 0x11223344ULL);
}

ZTEST(zzb_eui64, test_no_id_gives_enodev)
{
	uint64_t eui64;

	z_impl_hwinfo_get_device_id_fake.return_val = -ENOSYS;

	zassert_equal(zzb_eui64_get(&eui64), -ENODEV);
}

ZTEST_SUITE(zzb_eui64, NULL, NULL, before, NULL, NULL);
