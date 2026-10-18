/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "nwk/nwk.h"

static struct zzb_node node;

static void before(void *fixture)
{
	memset(&node, 0, sizeof(node));
}

ZTEST(zzb_nwk_neighbor, test_find_and_remove)
{
	struct zzb_nwk_neighbor *parent =
		zzb_nwk_neighbor_add(&node, 0x00124b001e17efa8ULL, 0x0000, ZZB_NWK_REL_PARENT);
	struct zzb_nwk_neighbor *sibling =
		zzb_nwk_neighbor_add(&node, 0x0011223344556677ULL, 0x1234, ZZB_NWK_REL_SIBLING);

	zassert_not_null(parent);
	zassert_not_null(sibling);
	zassert_equal(parent->short_addr, 0x0000);
	zassert_equal(sibling->ext_addr, 0x0011223344556677ULL);

	zassert_equal_ptr(zzb_nwk_neighbor_find_ext(&node, 0x0011223344556677ULL), sibling);
	zassert_equal_ptr(zzb_nwk_neighbor_find_short(&node, 0x0000), parent);
	zassert_equal_ptr(zzb_nwk_neighbor_parent(&node), parent);
	zassert_is_null(zzb_nwk_neighbor_find_short(&node, 0x5678));

	zzb_nwk_neighbor_remove(parent);
	zassert_is_null(zzb_nwk_neighbor_find_ext(&node, 0x00124b001e17efa8ULL));
	zassert_is_null(zzb_nwk_neighbor_find_short(&node, 0x0000));
	zassert_is_null(zzb_nwk_neighbor_parent(&node));
}

ZTEST(zzb_nwk_neighbor, test_full_table)
{
	struct zzb_nwk_neighbor *first = NULL;

	for (int i = 0; i < CONFIG_ZZB_NEIGHBOR_COUNT; i++) {
		struct zzb_nwk_neighbor *nb =
			zzb_nwk_neighbor_add(&node, 0x100 + i, 0x1000 + i, ZZB_NWK_REL_CHILD);

		zassert_not_null(nb, "entry %d", i);
		first = first ? first : nb;
	}

	zassert_is_null(zzb_nwk_neighbor_add(&node, 0x200, 0x2000, ZZB_NWK_REL_CHILD));

	zzb_nwk_neighbor_remove(first);
	zassert_not_null(zzb_nwk_neighbor_add(&node, 0x200, 0x2000, ZZB_NWK_REL_CHILD));
}

ZTEST(zzb_nwk_neighbor, test_new_entry_is_cleared)
{
	struct zzb_nwk_neighbor *nb = zzb_nwk_neighbor_add(&node, 0x100, 0x1000, ZZB_NWK_REL_CHILD);

	nb->lqi = 200;
	nb->in_fc[0] = 10;
	nb->in_fc[1] = 20;
	zzb_nwk_neighbor_remove(nb);

	nb = zzb_nwk_neighbor_add(&node, 0x101, 0x1001, ZZB_NWK_REL_CHILD);
	zassert_equal(nb->lqi, 0);
	zassert_equal(nb->in_fc[0], 0);
	zassert_equal(nb->in_fc[1], 0);
}

ZTEST_SUITE(zzb_nwk_neighbor, NULL, NULL, before, NULL, NULL);
