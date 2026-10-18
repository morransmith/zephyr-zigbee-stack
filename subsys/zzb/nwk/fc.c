/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "storage/settings.h"

#define JUMP CONFIG_ZZB_FRAME_COUNTER_JUMP

/* Version byte and watermark */
#define RECORD_KEY     "fc/nwk"
#define RECORD_VERSION 1
#define RECORD_LEN     5

static uint32_t jump_ahead(uint32_t value)
{
	return value > UINT32_MAX - JUMP ? UINT32_MAX : value + JUMP;
}

static int save(struct zzb_node *node, uint32_t watermark)
{
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION};

	sys_put_le32(watermark, rec + 1);
	return zzb_settings_save(node, RECORD_KEY, rec, sizeof(rec));
}

static void save_handler(struct k_work *work)
{
	struct zzb_node *node = CONTAINER_OF(work, struct zzb_node, nwk.fc.save);
	struct zzb_nwk_fc *fc = &node->nwk.fc;
	uint32_t watermark = jump_ahead(fc->value);

	fc->saving = false;
	if (save(node, watermark) == 0) {
		fc->watermark = watermark;
	}
}

void zzb_nwk_fc_init(struct zzb_node *node)
{
	node->nwk.fc = (struct zzb_nwk_fc){0};
	k_work_init(&node->nwk.fc.save, save_handler);
}

int zzb_nwk_fc_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t rec[RECORD_LEN];

	if (len != sizeof(rec) || read_cb(cb_arg, rec, sizeof(rec)) != sizeof(rec) ||
	    rec[0] != RECORD_VERSION) {
		return -EINVAL;
	}

	node->nwk.fc.watermark = sys_get_le32(rec + 1);
	return 0;
}

int zzb_nwk_fc_start(struct zzb_node *node)
{
	struct zzb_nwk_fc *fc = &node->nwk.fc;
	uint32_t watermark = jump_ahead(fc->watermark);
	int err;

	fc->value = fc->watermark;
	err = save(node, watermark);
	if (err) {
		return err;
	}

	fc->watermark = watermark;
	return 0;
}

static void schedule_save(struct zzb_nwk_fc *fc)
{
	if (fc->saving) {
		return;
	}

	fc->saving = true;
	k_work_submit_to_queue(&zzb_workq, &fc->save);
}

int zzb_nwk_fc_next(struct zzb_node *node, uint32_t *value)
{
	struct zzb_nwk_fc *fc = &node->nwk.fc;

	if (fc->value == UINT32_MAX) {
		return -EOVERFLOW;
	}

	if (fc->value >= fc->watermark) {
		schedule_save(fc);
		return -EBUSY;
	}

	*value = fc->value++;
	if (fc->value >= fc->watermark - JUMP / 2) {
		schedule_save(fc);
	}

	return 0;
}
