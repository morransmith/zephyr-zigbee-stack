/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
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
	/* A save scheduled by the previous test runs before the records are deleted */
	run_stack();
	zzb_aps_reset(node);
	zassert_ok(reboot(node));
}

/* R22 2.2.4.5.1, 2.2.4.5.3 */
ZTEST(zzb_aps_group, test_add_and_remove)
{
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 2));
	/* The same membership again takes no entry */
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 1));
	zassert_equal(zzb_aps_group_capacity(node), CONFIG_ZZB_APS_GROUP_COUNT - 2);
	zassert_true(zzb_aps_group_member(node, 0x0001, 1));
	zassert_true(zzb_aps_group_member(node, 0x0001, 2));
	zassert_false(zzb_aps_group_member(node, 0x0002, 1));

	zassert_ok(zzb_apsme_remove_group(node, 0x0001, 1));
	zassert_false(zzb_aps_group_member(node, 0x0001, 1));
	zassert_true(zzb_aps_group_member(node, 0x0001, 2));
	zassert_equal(zzb_apsme_remove_group(node, 0x0001, 1), -ENOENT);
}

/* R22 2.2.4.5.5: only the memberships of the endpoint go */
ZTEST(zzb_aps_group, test_remove_all_groups)
{
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x0002, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 2));

	zzb_apsme_remove_all_groups(node, 1);
	zassert_false(zzb_aps_group_member(node, 0x0001, 1));
	zassert_false(zzb_aps_group_member(node, 0x0002, 1));
	zassert_true(zzb_aps_group_member(node, 0x0001, 2));
}

ZTEST(zzb_aps_group, test_full_table)
{
	for (int i = 0; i < CONFIG_ZZB_APS_GROUP_COUNT; i++) {
		zassert_ok(zzb_apsme_add_group(node, 0x0100 + i, 1));
	}

	zassert_equal(zzb_aps_group_capacity(node), 0);
	zassert_equal(zzb_apsme_add_group(node, 0x0001, 1), -ENOMEM);
	/* A full table accepts a membership it holds */
	zassert_ok(zzb_apsme_add_group(node, 0x0100, 1));
}

ZTEST(zzb_aps_group, test_groups_of_an_endpoint)
{
	uint16_t groups[3];

	zassert_ok(zzb_apsme_add_group(node, 0x0001, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x0002, 2));
	zassert_ok(zzb_apsme_add_group(node, 0x0003, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x0004, 1));

	zassert_equal(zzb_aps_groups_of(node, 1, groups, ARRAY_SIZE(groups)), 3);
	zassert_equal(groups[0], 0x0001);
	zassert_equal(groups[1], 0x0003);
	zassert_equal(groups[2], 0x0004);
	zassert_equal(zzb_aps_groups_of(node, 1, groups, 2), 2);
	zassert_equal(zzb_aps_groups_of(node, 3, groups, ARRAY_SIZE(groups)), 0);
}

/* R22 2.2.8.1: the group table is persistent */
ZTEST(zzb_aps_group, test_table_survives_a_reboot)
{
	zassert_ok(zzb_apsme_add_group(node, 0x0001, 1));
	zassert_ok(zzb_apsme_add_group(node, 0x1234, 2));
	zassert_ok(zzb_apsme_remove_group(node, 0x0001, 1));
	zassert_ok(reboot(node));

	zassert_false(zzb_aps_group_member(node, 0x0001, 1));
	zassert_true(zzb_aps_group_member(node, 0x1234, 2));
	zassert_equal(zzb_aps_group_capacity(node), CONFIG_ZZB_APS_GROUP_COUNT - 1);

	zzb_apsme_remove_all_groups(node, 2);
	zassert_ok(reboot(node));
	zassert_equal(zzb_aps_group_capacity(node), CONFIG_ZZB_APS_GROUP_COUNT);

	zassert_ok(zzb_apsme_add_group(node, 0x0005, 1));
	zzb_aps_reset(node);
	zassert_false(zzb_aps_group_member(node, 0x0005, 1));
	zassert_ok(reboot(node));
	zassert_false(zzb_aps_group_member(node, 0x0005, 1));
}

ZTEST(zzb_aps_group, test_unknown_record_version)
{
	uint8_t rec[4] = {2};

	zassert_ok(settings_save_one("zzb/0/aps/group/0", rec, sizeof(rec)));
	zassert_equal(reboot(node), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/aps/group/0", rec, sizeof(rec) - 1));
	zassert_equal(reboot(node), -EINVAL);

	/* The next suite needs a node that booted */
	zassert_ok(settings_delete("zzb/0/aps/group/0"));
	zassert_ok(reboot(node));
}

ZTEST_SUITE(zzb_aps_group, NULL, setup, before, NULL, NULL);
