/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "nwk/nwk.h"

static struct zzb_node node;
static struct zzb_nib *const nib = &node.nwk.nib;

static const uint8_t key_a[ZZB_SEC_KEY_LEN] = {0xaa};
static const uint8_t key_b[ZZB_SEC_KEY_LEN] = {0xbb};
static const uint8_t key_c[ZZB_SEC_KEY_LEN] = {0xcc};

static void before(void *fixture)
{
	memset(&node, 0, sizeof(node));
	reset_fakes();
	zzb_nwk_init(&node);
}

static void check_key(int slot, const uint8_t *key, uint8_t seq)
{
	zassert_true(nib->keys[slot].valid, "slot %d", slot);
	zassert_equal(nib->keys[slot].seq, seq, "slot %d", slot);
	zassert_mem_equal(nib->keys[slot].key, key, ZZB_SEC_KEY_LEN, "slot %d", slot);
}

ZTEST(zzb_nwk_nib, test_defaults)
{
	zassert_equal(nib->pan_id, 0xffff);
	zassert_equal(nib->short_addr, 0xffff);
	zassert_false(nib->keys[0].valid);
	zassert_false(nib->keys[1].valid);
}

ZTEST(zzb_nwk_nib, test_addresses_are_set_in_the_mac)
{
	zzb_nlme_set_pan_id(&node, 0x1a62);
	zzb_nlme_set_short_addr(&node, 0x5678);
	zzb_nlme_set_ext_pan_id(&node, 0x0102030405060708ULL);

	zassert_equal(nib->pan_id, 0x1a62);
	zassert_equal(nib->short_addr, 0x5678);
	zassert_equal(nib->ext_pan_id, 0x0102030405060708ULL);
	zassert_equal(zzb_mlme_set_pan_id_fake.call_count, 1);
	zassert_equal(zzb_mlme_set_pan_id_fake.arg1_val, 0x1a62);
	zassert_equal(zzb_mlme_set_short_addr_fake.call_count, 1);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0x5678);
}

/* The first key becomes active; a new key goes to the slot that is not active */
ZTEST(zzb_nwk_nib, test_key_slots)
{
	zzb_nlme_set_nwk_key(&node, key_a, 1);
	check_key(0, key_a, 1);
	zassert_equal(nib->active_key, 0);
	zassert_equal(nib->newest_key, 0);

	zzb_nlme_set_nwk_key(&node, key_b, 2);
	check_key(1, key_b, 2);
	zassert_equal(nib->active_key, 0);
	zassert_equal(nib->newest_key, 1);

	zzb_nlme_set_nwk_key(&node, key_c, 3);
	check_key(0, key_a, 1);
	check_key(1, key_c, 3);
	zassert_equal(nib->active_key, 0);

	/* The same sequence number replaces the active key */
	zzb_nlme_set_nwk_key(&node, key_b, 1);
	check_key(0, key_b, 1);
	zassert_equal(nib->newest_key, 0);
}

ZTEST(zzb_nwk_nib, test_key_resets_incoming_counters_of_its_slot)
{
	struct zzb_nwk_neighbor *nb =
		zzb_nwk_neighbor_add(&node, 0x100, 0x1000, ZZB_NWK_REL_PARENT);

	zzb_nlme_set_nwk_key(&node, key_a, 1);
	nb->in_fc[0] = 10;
	nb->in_fc[1] = 20;

	zzb_nlme_set_nwk_key(&node, key_b, 2);
	zassert_equal(nb->in_fc[0], 10);
	zassert_equal(nb->in_fc[1], 0);
}

ZTEST(zzb_nwk_nib, test_key_ends_the_pending_state)
{
	node.nwk.key_pending = true;
	zzb_nlme_set_nwk_key(&node, key_a, 1);
	zassert_false(node.nwk.key_pending);
}

ZTEST_SUITE(zzb_nwk_nib, NULL, NULL, before, NULL, NULL);
