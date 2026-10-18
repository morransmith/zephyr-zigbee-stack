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

ZTEST_SUITE(zzb_node, NULL, NULL, NULL, NULL, NULL);
