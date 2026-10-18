/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "sec/mmo.h"

/* R22 C.5.1 */
ZTEST(zzb_mmo, test_one_byte_message)
{
	static const uint8_t msg[] = {0xc0};
	static const uint8_t expected[] = {
		0xae, 0x3a, 0x10, 0x2a, 0x28, 0xd4, 0x3e, 0xe0,
		0xd4, 0xa0, 0x9e, 0x22, 0x78, 0x8b, 0x20, 0x6c,
	};
	uint8_t hash[ZZB_SEC_KEY_LEN];

	zassert_ok(zzb_sec_mmo(msg, sizeof(msg), hash));
	zassert_mem_equal(hash, expected, sizeof(expected));
}

/* R22 C.5.2: the padding takes a block of its own */
ZTEST(zzb_mmo, test_one_block_message)
{
	static const uint8_t msg[] = {
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	};
	static const uint8_t expected[] = {
		0xa7, 0x97, 0x7e, 0x88, 0xbc, 0x0b, 0x61, 0xe8,
		0x21, 0x08, 0x27, 0x10, 0x9a, 0x22, 0x8f, 0x2d,
	};
	uint8_t hash[ZZB_SEC_KEY_LEN];

	zassert_ok(zzb_sec_mmo(msg, sizeof(msg), hash));
	zassert_mem_equal(hash, expected, sizeof(expected));
}

/* R22 C.5.3: 0x80 ends a block and the length takes an extra block */
ZTEST(zzb_mmo, test_longest_message)
{
	static const uint8_t expected[] = {
		0x24, 0xec, 0x2f, 0xe7, 0x5b, 0xbf, 0xfc, 0xb3,
		0x47, 0x89, 0xbc, 0x06, 0x10, 0xe7, 0xf1, 0x65,
	};
	static uint8_t msg[8192];
	uint8_t hash[ZZB_SEC_KEY_LEN];

	for (size_t i = 0; i < sizeof(msg); i++) {
		msg[i] = i;
	}

	zassert_ok(zzb_sec_mmo(msg, sizeof(msg) - 1, hash));
	zassert_mem_equal(hash, expected, sizeof(expected));
	zassert_equal(zzb_sec_mmo(msg, sizeof(msg), hash), -EINVAL);
}

ZTEST_SUITE(zzb_mmo, NULL, NULL, NULL, NULL, NULL);
