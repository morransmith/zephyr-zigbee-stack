/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "aps/aps.h"
#include "common/node.h"
#include "storage/settings.h"

#define RECORD_VERSION 1
#define RECORD_LEN     4
/* Room for aps/group/<i> with any int i */
#define NAME_LEN       22

static void record_name(char name[NAME_LEN], int index)
{
	snprintk(name, NAME_LEN, "aps/group/%d", index);
}

static void save(struct zzb_node *node, const struct zzb_aps_group *entry)
{
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION};
	char name[NAME_LEN];

	sys_put_le16(entry->group, rec + 1);
	rec[3] = entry->ep;
	record_name(name, entry - node->aps.aib.groups);
	zzb_settings_save(node, name, rec, sizeof(rec));
}

static void delete(struct zzb_node *node, struct zzb_aps_group *entry)
{
	char name[NAME_LEN];

	entry->in_use = false;
	record_name(name, entry - node->aps.aib.groups);
	zzb_settings_delete(node, name);
}

static struct zzb_aps_group *find(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		if (entry->in_use && entry->group == group && entry->ep == ep) {
			return entry;
		}
	}

	return NULL;
}

int zzb_apsme_add_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	if (find(node, group, ep) != NULL) {
		return 0;
	}

	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		if (!entry->in_use) {
			*entry = (struct zzb_aps_group){
				.in_use = true,
				.group = group,
				.ep = ep,
			};
			save(node, entry);
			return 0;
		}
	}

	return -ENOMEM;
}

int zzb_apsme_remove_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	struct zzb_aps_group *entry = find(node, group, ep);

	if (entry == NULL) {
		return -ENOENT;
	}

	delete(node, entry);
	return 0;
}

void zzb_apsme_remove_all_groups(struct zzb_node *node, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		if (entry->in_use && entry->ep == ep) {
			delete(node, entry);
		}
	}
}

bool zzb_aps_group_member(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	return find(node, group, ep) != NULL;
}

int zzb_aps_group_capacity(struct zzb_node *node)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		count += !entry->in_use;
	}

	return count;
}

int zzb_aps_groups_of(struct zzb_node *node, uint8_t ep, uint16_t *groups, int max)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		if (count == max) {
			break;
		}

		if (entry->in_use && entry->ep == ep) {
			groups[count++] = entry->group;
		}
	}

	return count;
}

void zzb_aps_group_delete(struct zzb_node *node)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.groups, entry) {
		delete(node, entry);
	}
}

int zzb_aps_group_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
		       void *cb_arg)
{
	uint8_t rec[RECORD_LEN];
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->aps.aib.groups)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	node->aps.aib.groups[index] = (struct zzb_aps_group){
		.in_use = true,
		.group = sys_get_le16(rec + 1),
		.ep = rec[3],
	};
	return 0;
}
