/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

#define JUMP CONFIG_ZZB_FRAME_COUNTER_JUMP
/* The only node, index 0 in the pool */
#define KEY  "zzb/0/fc/nwk"

static struct zzb_node *node;

struct call {
	struct k_work work;
	int err;
	uint32_t count;
	uint32_t last;
};

/* What zzb_node_init() does for NWK on boot */
static void reboot_handler(struct k_work *work)
{
	struct call *call = CONTAINER_OF(work, struct call, work);

	zzb_nwk_init(node);
	call->err = zzb_settings_load(node);
	if (call->err == 0) {
		call->err = zzb_nwk_fc_start(node);
	}
}

static int reboot(void)
{
	struct call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

/* Takes counters on the stack thread until one fails, so a scheduled save cannot run between */
static void take_all_handler(struct k_work *work)
{
	struct call *call = CONTAINER_OF(work, struct call, work);
	uint32_t fc;

	while ((call->err = zzb_nwk_fc_next(node, &fc)) == 0) {
		call->count++;
		call->last = fc;
	}
}

/* The test thread is cooperative and has a lower priority than the stack thread */
static void run_stack(void)
{
	k_yield();
}

static uint32_t next(void)
{
	uint32_t fc;

	zassert_ok(zzb_nwk_fc_next(node, &fc));
	return fc;
}

static uint32_t stored_watermark(void)
{
	uint8_t rec[5];

	zassert_equal(settings_load_one(KEY, rec, sizeof(rec)), sizeof(rec));
	zassert_equal(rec[0], 1);
	return sys_get_le32(rec + 1);
}

static void store(uint8_t version, uint32_t watermark)
{
	uint8_t rec[5] = {version};

	sys_put_le32(watermark, rec + 1);
	zassert_ok(settings_save_one(KEY, rec, sizeof(rec)));
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = 0x0011223344556677ULL,
	};

	zassert_ok(zzb_node_init(&cfg, &node));
	return NULL;
}

static void before(void *fixture)
{
	/* A save scheduled by the previous test runs before the record is deleted */
	run_stack();
	settings_delete(KEY);
	zassert_ok(reboot());
}

ZTEST(zzb_nwk_fc, test_first_boot)
{
	zassert_equal(next(), 0);
	zassert_equal(next(), 1);
	zassert_equal(stored_watermark(), JUMP);
}

/* Counters taken before a reboot are never used again */
ZTEST(zzb_nwk_fc, test_reboot_jumps_to_the_watermark)
{
	next();
	next();
	zassert_ok(reboot());

	zassert_equal(next(), JUMP);
	zassert_equal(stored_watermark(), 2 * JUMP);
}

ZTEST(zzb_nwk_fc, test_watermark_moves_at_half_jump)
{
	for (int i = 0; i < JUMP / 2; i++) {
		next();
	}

	run_stack();
	zassert_equal(stored_watermark(), JUMP / 2 + JUMP);

	for (int i = JUMP / 2; i <= JUMP; i++) {
		zassert_equal(next(), i);
	}
}

ZTEST(zzb_nwk_fc, test_busy_until_the_watermark_is_saved)
{
	struct call call = {0};

	zzb_call_sync(&call.work, take_all_handler);
	zassert_equal(call.err, -EBUSY);
	zassert_equal(call.count, JUMP);
	zassert_equal(call.last, JUMP - 1);

	/* Runs the save scheduled at JUMP / 2 */
	run_stack();
	zassert_equal(next(), JUMP);
}

ZTEST(zzb_nwk_fc, test_last_counter)
{
	uint32_t fc;

	store(1, UINT32_MAX - 1);
	zassert_ok(reboot());

	zassert_equal(next(), UINT32_MAX - 1);
	zassert_equal(zzb_nwk_fc_next(node, &fc), -EOVERFLOW);
}

ZTEST(zzb_nwk_fc, test_unknown_record_version)
{
	store(2, 100);
	zassert_equal(reboot(), -EINVAL);
}

ZTEST_SUITE(zzb_nwk_fc, NULL, setup, before, NULL, NULL);
