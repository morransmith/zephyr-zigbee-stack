/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

/* Node 0 of the pool, since the records are saved under its index */
static struct zzb_node *node;
static struct zzb_nib *nib;

static const uint8_t key_a[ZZB_SEC_KEY_LEN] = {0xaa};
static const uint8_t key_b[ZZB_SEC_KEY_LEN] = {0xbb};
static const uint8_t key_c[ZZB_SEC_KEY_LEN] = {0xcc};

static void *setup(void)
{
	node = test_node(0);
	nib = &node->nwk.nib;
	return NULL;
}

static void before(void *fixture)
{
	delete_nwk_records();
	reset_fakes();
	zzb_nwk_init(node);
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
	zzb_nlme_set_pan_id(node, 0x1a62);
	zzb_nlme_set_short_addr(node, 0x5678);
	zzb_nlme_set_ext_pan_id(node, 0x0102030405060708ULL);

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
	zzb_nlme_set_nwk_key(node, key_a, 1);
	check_key(0, key_a, 1);
	zassert_equal(nib->active_key, 0);
	zassert_equal(nib->newest_key, 0);

	zzb_nlme_set_nwk_key(node, key_b, 2);
	check_key(1, key_b, 2);
	zassert_equal(nib->active_key, 0);
	zassert_equal(nib->newest_key, 1);

	zzb_nlme_set_nwk_key(node, key_c, 3);
	check_key(0, key_a, 1);
	check_key(1, key_c, 3);
	zassert_equal(nib->active_key, 0);

	/* The same sequence number replaces the active key */
	zzb_nlme_set_nwk_key(node, key_b, 1);
	check_key(0, key_b, 1);
	zassert_equal(nib->newest_key, 0);
}

ZTEST(zzb_nwk_nib, test_key_resets_incoming_counters_of_its_slot)
{
	struct zzb_nwk_neighbor *nb = zzb_nwk_neighbor_add(node, 0x100, 0x1000, ZZB_NWK_REL_PARENT);

	zzb_nlme_set_nwk_key(node, key_a, 1);
	nb->in_fc[0] = 10;
	nb->in_fc[1] = 20;

	zzb_nlme_set_nwk_key(node, key_b, 2);
	zassert_equal(nb->in_fc[0], 10);
	zassert_equal(nb->in_fc[1], 0);
}

ZTEST(zzb_nwk_nib, test_key_ends_the_pending_state)
{
	node->nwk.key_pending = true;
	zzb_nlme_set_nwk_key(node, key_a, 1);
	zassert_false(node->nwk.key_pending);
}

/* A node on a network as join and rejoin leave it */
static struct zzb_nwk_neighbor *join(void)
{
	struct zzb_nwk_neighbor *parent;

	zzb_nlme_set_pan_id(node, 0x1a62);
	zzb_nlme_set_short_addr(node, 0x5678);
	zzb_nlme_set_ext_pan_id(node, 0x0102030405060708ULL);
	nib->channel = 20;
	nib->update_id = 4;
	nib->capability = 0x88;
	nib->parent_info = 0x01;
	parent = zzb_nwk_neighbor_add(node, 0x00124b001e17efa8ULL, 0x1111, ZZB_NWK_REL_PARENT);
	parent->device_type = ZZB_NWK_DEVICE_ROUTER;
	zzb_nlme_set_nwk_key(node, key_a, 1);
	zzb_nlme_set_nwk_key(node, key_b, 2);
	return parent;
}

ZTEST(zzb_nwk_nib, test_records_survive_a_reboot)
{
	struct zzb_nwk_neighbor *parent;

	join();
	zzb_nwk_save(node);
	zassert_ok(nwk_reboot());

	zassert_equal(nib->pan_id, 0x1a62);
	zassert_equal(nib->short_addr, 0x5678);
	zassert_equal(nib->ext_pan_id, 0x0102030405060708ULL);
	zassert_equal(nib->channel, 20);
	zassert_equal(nib->update_id, 4);
	zassert_equal(nib->capability, 0x88);
	zassert_equal(nib->parent_info, 0x01);
	check_key(0, key_a, 1);
	check_key(1, key_b, 2);
	zassert_equal(nib->active_key, 0);
	zassert_equal(nib->newest_key, 1);

	parent = zzb_nwk_neighbor_parent(node);
	zassert_not_null(parent);
	zassert_equal(parent->ext_addr, 0x00124b001e17efa8ULL);
	zassert_equal(parent->short_addr, 0x1111);
	zassert_equal(parent->device_type, ZZB_NWK_DEVICE_ROUTER);
	zassert_equal(parent->beacon.ext_pan_id, 0x0102030405060708ULL);
	zassert_equal(parent->pan_id, 0x1a62);
	zassert_equal(parent->channel, 20);

	/* The MAC gets the network back, and the keepalive starts (R22 3.6.10.8) */
	zassert_equal(zzb_mlme_set_pan_id_fake.arg1_val, 0x1a62);
	zassert_equal(zzb_mlme_set_short_addr_fake.arg1_val, 0x5678);
	zassert_equal(zzb_mlme_set_channel_fake.arg1_val, 20);
	zassert_true(k_work_delayable_is_pending(&node->nwk.keepalive));
}

/* NLME-SET of nwkActiveKeySeqNumber after Switch Key (R22 4.6.3.4.2) */
ZTEST(zzb_nwk_nib, test_active_key)
{
	uint8_t rec[64];

	join();
	zassert_equal(zzb_nlme_set_active_key(node, 3), -ENOENT);
	zassert_equal(nib->active_key, 0);

	zassert_ok(zzb_nlme_set_active_key(node, 2));
	zassert_equal(nib->active_key, 1);
	zassert_equal(load_record("nwk/keys", rec, sizeof(rec)), 39);
	zassert_equal(rec[1], 1);
}

/* Channel change after Mgmt_NWK_Update_req (R22 Annex E) */
ZTEST(zzb_nwk_nib, test_channel)
{
	uint8_t rec[64];

	join();
	zassert_equal(zzb_nlme_set_channel(node, 10, 5), -EINVAL);
	zassert_equal(zzb_nlme_set_channel(node, 27, 5), -EINVAL);

	/* A busy MAC leaves the NIB on the channel of the radio */
	zzb_mlme_set_channel_fake.return_val = -EBUSY;
	zassert_equal(zzb_nlme_set_channel(node, 15, 5), -EBUSY);
	zassert_equal(nib->channel, 20);
	zassert_equal(nib->update_id, 4);

	zzb_mlme_set_channel_fake.return_val = 0;
	zassert_ok(zzb_nlme_set_channel(node, 15, 5));
	zassert_equal(zzb_mlme_set_channel_fake.arg1_val, 15);
	zassert_equal(nib->channel, 15);
	zassert_equal(nib->update_id, 5);
	zassert_equal(load_record("nwk/net", rec, sizeof(rec)), 28);
	zassert_equal(rec[13], 15);
	zassert_equal(rec[14], 5);

	node->nwk.op = ZZB_NWK_OP_REJOIN;
	zassert_equal(zzb_nlme_set_channel(node, 16, 6), -EBUSY);
	zassert_equal(zzb_mlme_set_channel_fake.call_count, 2);
}

/* Records exist only on a network without a pending key */
ZTEST(zzb_nwk_nib, test_records_are_written_on_a_network)
{
	uint8_t rec[64];

	zzb_nlme_set_nwk_key(node, key_a, 1);
	zassert_equal(load_record("nwk/keys", rec, sizeof(rec)), 0);

	join();
	zassert_equal(load_record("nwk/keys", rec, sizeof(rec)), 39);
	zassert_equal(rec[0], 1);

	delete_nwk_records();
	node->nwk.key_pending = true;
	zzb_nwk_save(node);
	zassert_equal(load_record("nwk/net", rec, sizeof(rec)), 0);

	/* The network key ends the pending state */
	zzb_nlme_set_nwk_key(node, key_c, 3);
	zassert_equal(load_record("nwk/net", rec, sizeof(rec)), 28);
	zassert_equal(rec[0], 1);
	zassert_equal(load_record("nwk/keys", rec, sizeof(rec)), 39);
}

ZTEST(zzb_nwk_nib, test_unknown_record_version)
{
	uint8_t rec[64];

	join();
	zzb_nwk_save(node);
	zassert_equal(load_record("nwk/net", rec, sizeof(rec)), 28);
	rec[0] = 2;
	zassert_ok(settings_save_one("zzb/0/nwk/net", rec, 28));
	zassert_equal(nwk_reboot(), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/nwk/net", rec, 27));
	zassert_equal(nwk_reboot(), -EINVAL);
}

ZTEST_SUITE(zzb_nwk_nib, NULL, setup, before, NULL, NULL);
