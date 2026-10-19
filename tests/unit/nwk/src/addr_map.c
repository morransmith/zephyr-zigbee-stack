/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "nwk/nwk.h"

#define EXT_A 0x00124b00000000aaULL
#define EXT_B 0x00124b00000000bbULL

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node(0);
	return NULL;
}

static void before(void *fixture)
{
	zassert_ok(zzb_nlme_reset_req(node));
	zassert_ok(nwk_reboot());
}

static uint16_t find(uint64_t ext_addr)
{
	uint16_t addr;

	zassert_ok(zzb_nwk_addr_map_find(node, ext_addr, &addr));
	return addr;
}

ZTEST(zzb_nwk_addr_map, test_set_and_find)
{
	uint16_t addr;

	zassert_ok(zzb_nlme_set_addr_map(node, EXT_A, 0x1111));
	zassert_ok(zzb_nlme_set_addr_map(node, EXT_B, 0x2222));
	zassert_equal(find(EXT_A), 0x1111);
	zassert_equal(zzb_nwk_addr_map_ext(node, 0x2222), EXT_B);
	zassert_equal(zzb_nwk_addr_map_find(node, 0x00124b00000000ccULL, &addr), -ENOENT);
	zassert_equal(zzb_nwk_addr_map_ext(node, 0x3333), 0);

	/* A new address replaces the old one */
	zassert_ok(zzb_nlme_set_addr_map(node, EXT_A, 0x1112));
	zassert_equal(find(EXT_A), 0x1112);
	zassert_equal(zzb_nwk_addr_map_ext(node, 0x1111), 0);
}

ZTEST(zzb_nwk_addr_map, test_full_table)
{
	for (int i = 0; i < CONFIG_ZZB_NWK_ADDR_MAP_COUNT; i++) {
		zassert_ok(zzb_nlme_set_addr_map(node, EXT_A + i, 0x1000 + i));
	}

	zassert_equal(zzb_nlme_set_addr_map(node, EXT_B, 0x2222), -ENOMEM);
	zassert_ok(zzb_nlme_set_addr_map(node, EXT_A, 0x1fff));
}

/* The entries survive a reboot; a leave and NLME-RESET clear them */
ZTEST(zzb_nwk_addr_map, test_records)
{
	uint16_t addr;

	zassert_ok(zzb_nlme_set_addr_map(node, EXT_A, 0x1111));
	zassert_ok(nwk_reboot());
	zassert_equal(find(EXT_A), 0x1111);

	zassert_ok(zzb_nlme_reset_req(node));
	zassert_equal(zzb_nwk_addr_map_find(node, EXT_A, &addr), -ENOENT);
	zassert_ok(nwk_reboot());
	zassert_equal(zzb_nwk_addr_map_find(node, EXT_A, &addr), -ENOENT);
}

ZTEST(zzb_nwk_addr_map, test_unknown_record_version)
{
	uint8_t rec[11] = {2};

	zassert_ok(settings_save_one("zzb/0/nwk/addr/0", rec, sizeof(rec)));
	zassert_equal(nwk_reboot(), -EINVAL);

	rec[0] = 1;
	zassert_ok(settings_save_one("zzb/0/nwk/addr/0", rec, sizeof(rec) - 1));
	zassert_equal(nwk_reboot(), -EINVAL);

	zassert_ok(settings_delete("zzb/0/nwk/addr/0"));
	zassert_ok(nwk_reboot());
}

ZTEST_SUITE(zzb_nwk_addr_map, NULL, setup, before, NULL, NULL);
