/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"

#define JUMP CONFIG_ZZB_FRAME_COUNTER_JUMP

static struct zzb_node *node;

struct call {
	struct k_work work;
	int err;
};

/* What zzb_node_init() does for APS on boot */
static void reboot_handler(struct k_work *work)
{
	struct call *call = CONTAINER_OF(work, struct call, work);

	zzb_aps_init(node);
	call->err = zzb_settings_load(node);
	if (call->err == 0) {
		call->err = zzb_fc_start(&node->aps.fc);
	}
}

static int reboot(void)
{
	struct call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void *setup(void)
{
	node = test_node(0);
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	/* A save scheduled by the previous test runs before the records are deleted */
	run_stack();
	zzb_aps_reset(node);
	settings_delete("zzb/0/fc/aps");
	zassert_ok(reboot());
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

ZTEST(zzb_aps_keys, test_records_survive_a_reboot)
{
	struct zzb_aps_key_pair *pair;

	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zassert_ok(zzb_apsme_set_link_key(node, TC_EUI64, key_b, ZZB_APS_KEY_VERIFIED,
					  ZZB_APS_LINK_KEY_UNIQUE));
	zassert_ok(reboot());

	zassert_equal(node->aps.aib.tc_addr, TC_EUI64);
	pair = zzb_aps_key_pair_find(node, TC_EUI64);
	zassert_not_null(pair);
	zassert_mem_equal(pair->key, key_b, ZZB_SEC_KEY_LEN);
	zassert_equal(pair->attr, ZZB_APS_KEY_VERIFIED);
	zassert_equal(pair->type, ZZB_APS_LINK_KEY_UNIQUE);
}

ZTEST(zzb_aps_keys, test_reset_deletes_the_records)
{
	zassert_ok(zzb_apsme_set_tc_addr(node, TC_EUI64));
	zzb_aps_reset(node);

	zassert_equal(node->aps.aib.tc_addr, ZZB_APS_TC_UNKNOWN);
	zassert_is_null(zzb_aps_key_pair_find(node, TC_EUI64));
	zassert_ok(reboot());
	zassert_equal(node->aps.aib.tc_addr, ZZB_APS_TC_UNKNOWN);
	zassert_is_null(zzb_aps_key_pair_find(node, TC_EUI64));
}

/* The outgoing APS frame counter is never reused (R22 4.3.4 for NWK, the same rule here) */
ZTEST(zzb_aps_keys, test_reset_keeps_the_frame_counter)
{
	uint8_t rec[5];
	uint32_t fc;

	zassert_equal(settings_load_one("zzb/0/fc/aps", rec, sizeof(rec)), sizeof(rec));
	zassert_equal(sys_get_le32(rec + 1), JUMP);

	zassert_ok(zzb_fc_next(&node->aps.fc, &fc));
	zassert_equal(fc, 0);
	zzb_aps_reset(node);
	zassert_ok(zzb_fc_next(&node->aps.fc, &fc));
	zassert_equal(fc, 1);
}

ZTEST(zzb_aps_keys, test_unknown_record_version)
{
	uint8_t rec[9] = {2};

	zassert_ok(settings_save_one("zzb/0/aps/tc", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);
	zassert_ok(settings_delete("zzb/0/aps/tc"));

	/* A key record with the length of the trust center record */
	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/aps/key/0", rec, sizeof(rec)));
	zassert_equal(reboot(), -EINVAL);
}

ZTEST_SUITE(zzb_aps_keys, NULL, setup, before, NULL, NULL);
