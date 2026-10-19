/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "common/fc.h"
#include "common/queue.h"
#include "storage/settings.h"

#define JUMP CONFIG_ZZB_FRAME_COUNTER_JUMP

/* Version byte and watermark */
#define RECORD_VERSION 1
#define RECORD_LEN     5

static uint32_t jump_ahead(uint32_t value)
{
	return value > UINT32_MAX - JUMP ? UINT32_MAX : value + JUMP;
}

static int save(struct zzb_fc *fc, uint32_t watermark)
{
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION};

	sys_put_le32(watermark, rec + 1);
	return zzb_settings_save(fc->node, fc->key, rec, sizeof(rec));
}

static void save_handler(struct k_work *work)
{
	struct zzb_fc *fc = CONTAINER_OF(work, struct zzb_fc, save);
	uint32_t watermark = jump_ahead(fc->value);

	fc->saving = false;
	if (save(fc, watermark) == 0) {
		fc->watermark = watermark;
	}
}

void zzb_fc_init(struct zzb_fc *fc, struct zzb_node *node, const char *key)
{
	fc->node = node;
	fc->key = key;
	k_work_init(&fc->save, save_handler);
}

int zzb_fc_load(struct zzb_fc *fc, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t rec[RECORD_LEN];

	if (len != sizeof(rec) || read_cb(cb_arg, rec, sizeof(rec)) != sizeof(rec) ||
	    rec[0] != RECORD_VERSION) {
		return -EINVAL;
	}

	fc->watermark = sys_get_le32(rec + 1);
	return 0;
}

int zzb_fc_start(struct zzb_fc *fc)
{
	uint32_t watermark = jump_ahead(fc->watermark);
	int err;

	fc->value = fc->watermark;
	err = save(fc, watermark);
	if (err) {
		return err;
	}

	fc->watermark = watermark;
	return 0;
}

static void schedule_save(struct zzb_fc *fc)
{
	if (fc->saving) {
		return;
	}

	fc->saving = true;
	k_work_submit_to_queue(&zzb_workq, &fc->save);
}

int zzb_fc_next(struct zzb_fc *fc, uint32_t *value)
{
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
