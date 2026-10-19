/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_FC_H_
#define ZZB_SUBSYS_COMMON_FC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

struct zzb_node;

/* Outgoing frame counter, global to the node, and its saved watermark */
struct zzb_fc {
	uint32_t value;
	uint32_t watermark;
	bool saving;
	struct k_work save;
	struct zzb_node *node;
	/* Record key under zzb/<n> */
	const char *key;
};

void zzb_fc_init(struct zzb_fc *fc, struct zzb_node *node, const char *key);

/* Loads the watermark from the record of the counter */
int zzb_fc_load(struct zzb_fc *fc, size_t len, settings_read_cb read_cb, void *cb_arg);

/* Starts the counter at the loaded watermark and saves the next watermark */
int zzb_fc_start(struct zzb_fc *fc);

/*
 * Sets value to the next outgoing frame counter. Returns -EBUSY while the counter is at a
 * watermark that is not saved yet, or -EOVERFLOW once the counter is 2^32-1.
 */
int zzb_fc_next(struct zzb_fc *fc, uint32_t *value);

#endif /* ZZB_SUBSYS_COMMON_FC_H_ */
