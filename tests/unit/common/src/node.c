/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"

/* Relies on CONFIG_ZZB_NODE_COUNT=1 */
ZTEST(zzb_node, test_init_uses_cfg_eui64_until_pool_is_full)
{
	const struct zzb_node_config cfg = {
		.eui64 = 0x0011223344556677ULL,
	};
	struct zzb_node *node;
	struct zzb_node *extra;

	zassert_ok(zzb_node_init(&cfg, &node));
	zassert_equal(node->eui64, cfg.eui64);
	zassert_equal(zzb_node_init(&cfg, &extra), -ENOMEM);
	zassert_is_null(extra);
}

/* BDB 10.1.1: a code of 6, 8, 12 or 16 bytes and its CRC, checked before a node is taken */
ZTEST(zzb_node, test_install_code_is_checked)
{
	/* BDB 10.1.1 example with the last octet of the CRC changed */
	static const uint8_t bad_crc[] = {
		0x83, 0xfe, 0xd3, 0x40, 0x7a, 0x93, 0x97, 0x23, 0xa5,
		0xc6, 0x39, 0xb2, 0x69, 0x16, 0xd5, 0x05, 0xc3, 0xb4,
	};
	/* The CRC of the code 01 02 03 04 05 06 07, but a code of 7 bytes */
	static const uint8_t bad_len[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x01, 0x6d};
	struct zzb_node_config cfg = {
		.eui64 = 0x0011223344556677ULL,
		.install_code = bad_crc,
		.install_code_len = sizeof(bad_crc),
	};
	struct zzb_node *node;

	zassert_equal(zzb_node_init(&cfg, &node), -EINVAL);
	zassert_is_null(node);

	cfg.install_code = bad_len;
	cfg.install_code_len = sizeof(bad_len);
	zassert_equal(zzb_node_init(&cfg, &node), -EINVAL);
}

ZTEST_SUITE(zzb_node, NULL, NULL, NULL, NULL, NULL);
