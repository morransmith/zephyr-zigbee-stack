/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "common/node.h"
#include "fakes.h"

static struct zzb_node *node;

static const struct zzb_aps_binding to_tc = {
	.src_ep = 1,
	.cluster = 0x0006,
	.dst_mode = ZZB_APS_ADDR_EXT,
	.dst_ext = TC_EUI64,
	.dst_ep = 1,
};

static const struct zzb_aps_binding to_group = {
	.src_ep = 1,
	.cluster = 0x0006,
	.dst_mode = ZZB_APS_ADDR_GROUP,
	.dst_addr = 0x1234,
};

static int entries(void)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		count += entry->in_use;
	}

	return count;
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
	zassert_ok(reboot(node));
}

ZTEST(zzb_aps_binding, test_bind_and_unbind)
{
	zassert_ok(zzb_apsme_bind_req(node, &to_tc));
	zassert_ok(zzb_apsme_bind_req(node, &to_group));
	/* The same binding again adds no entry */
	zassert_ok(zzb_apsme_bind_req(node, &to_tc));
	zassert_equal(entries(), 2);

	zassert_ok(zzb_apsme_unbind_req(node, &to_tc));
	zassert_equal(zzb_apsme_unbind_req(node, &to_tc), -ENOENT);
	zassert_equal(entries(), 1);
}

/* The fields of the destination address mode identify an entry */
ZTEST(zzb_aps_binding, test_entry_identity)
{
	struct zzb_aps_binding other = to_tc;

	zassert_ok(zzb_apsme_bind_req(node, &to_tc));
	other.dst_ep = 2;
	zassert_ok(zzb_apsme_bind_req(node, &other));
	zassert_equal(entries(), 2);

	/* A group entry has no destination endpoint */
	zassert_ok(zzb_apsme_bind_req(node, &to_group));
	other = to_group;
	other.dst_ep = 5;
	zassert_ok(zzb_apsme_unbind_req(node, &other));
	zassert_equal(entries(), 2);
}

ZTEST(zzb_aps_binding, test_limits)
{
	struct zzb_aps_binding binding = to_tc;

	binding.dst_mode = ZZB_APS_ADDR_SHORT;
	zassert_equal(zzb_apsme_bind_req(node, &binding), -EINVAL);

	binding.dst_mode = ZZB_APS_ADDR_EXT;
	for (int i = 0; i < CONFIG_ZZB_APS_BINDING_COUNT; i++) {
		binding.cluster = i;
		zassert_ok(zzb_apsme_bind_req(node, &binding));
	}

	binding.cluster = 0xffff;
	zassert_equal(zzb_apsme_bind_req(node, &binding), -ENOMEM);

	/* A full table accepts a binding it holds */
	binding.cluster = 0;
	zassert_ok(zzb_apsme_bind_req(node, &binding));
}

/* ZHA writes bindings once, during pairing (section 14) */
ZTEST(zzb_aps_binding, test_table_survives_a_reboot)
{
	const struct zzb_aps_binding *entry = &node->aps.aib.bindings[1];

	zassert_ok(zzb_apsme_bind_req(node, &to_group));
	zassert_ok(zzb_apsme_bind_req(node, &to_tc));
	zassert_ok(zzb_apsme_unbind_req(node, &to_group));
	zassert_ok(reboot(node));

	zassert_equal(entries(), 1);
	zassert_true(entry->in_use);
	zassert_equal(entry->src_ep, 1);
	zassert_equal(entry->cluster, 0x0006);
	zassert_equal(entry->dst_mode, ZZB_APS_ADDR_EXT);
	zassert_equal(entry->dst_ext, TC_EUI64);
	zassert_equal(entry->dst_ep, 1);

	zassert_ok(zzb_apsme_bind_req(node, &to_group));
	zassert_ok(reboot(node));
	entry = &node->aps.aib.bindings[0];
	zassert_equal(entry->dst_mode, ZZB_APS_ADDR_GROUP);
	zassert_equal(entry->dst_addr, 0x1234);

	zzb_aps_reset(node);
	zassert_ok(reboot(node));
	zassert_equal(entries(), 0);
}

ZTEST(zzb_aps_binding, test_unknown_record_version)
{
	uint8_t rec[14] = {2};

	zassert_ok(settings_save_one("zzb/0/aps/bind/0", rec, sizeof(rec)));
	zassert_equal(reboot(node), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/aps/bind/0", rec, sizeof(rec) - 1));
	zassert_equal(reboot(node), -EINVAL);

	/* The next suite needs a node that booted */
	zassert_ok(settings_delete("zzb/0/aps/bind/0"));
	zassert_ok(reboot(node));
}

ZTEST_SUITE(zzb_aps_binding, NULL, setup, before, NULL, NULL);
