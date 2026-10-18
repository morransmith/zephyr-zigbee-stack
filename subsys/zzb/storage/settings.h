/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_STORAGE_SETTINGS_H_
#define ZZB_SUBSYS_STORAGE_SETTINGS_H_

#include <stddef.h>

struct zzb_node;

/* Loads the records under zzb/<n> and passes each to its layer; returns the first error. */
int zzb_settings_load(struct zzb_node *node);

/* Saves value under zzb/<n>/key. */
int zzb_settings_save(struct zzb_node *node, const char *key, const void *value, size_t len);

/* Deletes the record zzb/<n>/key. */
int zzb_settings_delete(struct zzb_node *node, const char *key);

#endif /* ZZB_SUBSYS_STORAGE_SETTINGS_H_ */
