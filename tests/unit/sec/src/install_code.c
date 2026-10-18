/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "sec/install_code.h"

/* BDB 10.1.1 and 10.1.2: "83FE D340 7A93 9723 A5C6 39B2 6916 D505 C3B5" */
static const uint8_t code[] = {
	0x83, 0xfe, 0xd3, 0x40, 0x7a, 0x93, 0x97, 0x23, 0xa5,
	0xc6, 0x39, 0xb2, 0x69, 0x16, 0xd5, 0x05, 0xc3, 0xb5,
};

ZTEST(zzb_install_code, test_bdb_example)
{
	static const uint8_t expected[] = {
		0x66, 0xb6, 0x90, 0x09, 0x81, 0xe1, 0xee, 0x3c,
		0xa4, 0x20, 0x6b, 0x6b, 0x86, 0x1c, 0x02, 0xbb,
	};
	uint8_t key[ZZB_SEC_KEY_LEN];

	zassert_ok(zzb_sec_install_code_key(code, sizeof(code), key));
	zassert_mem_equal(key, expected, sizeof(expected));
}

ZTEST(zzb_install_code, test_wrong_crc_is_rejected)
{
	uint8_t bad[sizeof(code)];
	uint8_t key[ZZB_SEC_KEY_LEN];

	memcpy(bad, code, sizeof(code));
	bad[sizeof(bad) - 1] ^= 0x01;

	zassert_equal(zzb_sec_install_code_key(bad, sizeof(bad), key), -EINVAL);
}

ZTEST_SUITE(zzb_install_code, NULL, NULL, NULL, NULL, NULL);
