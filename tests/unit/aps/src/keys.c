/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "common/node.h"
#include "fakes.h"

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node(0);
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	zzb_aps_init(node);
}

ZTEST(zzb_aps_keys, test_trust_center_unknown_after_init)
{
	zassert_equal(node->aps.aib.tc_addr, ZZB_APS_TC_UNKNOWN);
	zassert_is_null(zzb_aps_key_pair_find(node, TC_EUI64));
}

/* The entry joins the trust center that sent the network key (R22 4.6.3.2.3) */
ZTEST(zzb_aps_keys, test_trust_center_address_adds_the_default_key)
{
	struct zzb_aps_key_pair *pair;

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zassert_equal(node->aps.aib.tc_addr, TC_EUI64);

	pair = zzb_aps_key_pair_find(node, TC_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, zzb_aps_default_tc_link_key, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_PROVISIONAL);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_GLOBAL);
	zassert_equal(pair->in_fc, 0);
}

/* A trust center rejoin keeps the updated link key */
ZTEST(zzb_aps_keys, test_trust_center_address_keeps_the_link_key)
{
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));

	zassert_mem_equal(zzb_aps_key_pair_find(node, TC_EUI64)->key, key_b, ZZB_SEC_KEY_LEN);
}

/* BDB 10.2.5 step 9 */
ZTEST(zzb_aps_keys, test_link_key_replaces_the_entry)
{
	struct zzb_aps_key_pair *pair;

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	pair = zzb_aps_key_pair_find(node, TC_EUI64);
	pair->in_fc = 100;

	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_UNVERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_equal_ptr(zzb_aps_key_pair_find(node, TC_EUI64), pair);
	zassert_mem_equal(pair->key, key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_UNVERIFIED);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_UNIQUE);
	zassert_equal(pair->in_fc, 0);
}

ZTEST(zzb_aps_keys, test_full_table)
{
	for (int i = 0; i < CONFIG_ZZB_APS_KEY_PAIR_COUNT; i++) {
		zassert_ok(zzb_apsme_set_link_key(node, 0x100 + i, key_b, ZZB_APS_KEY_VERIFIED,
						  ZZB_APS_LINK_KEY_UNIQUE));
	}

	zassert_equal(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					     ZZB_APS_LINK_KEY_UNIQUE),
		      -ENOMEM);
	zassert_equal(zzb_apsme_set_tc_addr(node, TC_EUI64), -ENOMEM);
	zassert_equal(node->aps.aib.tc_addr, ZZB_APS_TC_UNKNOWN);
}

ZTEST_SUITE(zzb_aps_keys, NULL, setup, before, NULL, NULL);
