/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include "aps/aps.h"
#include "common/node.h"

static bool same(const struct zzb_aps_binding *a, const struct zzb_aps_binding *b)
{
	if (a->src_ep != b->src_ep || a->cluster != b->cluster || a->dst_mode != b->dst_mode) {
		return false;
	}

	if (a->dst_mode == ZZB_APS_ADDR_GROUP) {
		return a->dst_addr == b->dst_addr;
	}

	return a->dst_ext == b->dst_ext && a->dst_ep == b->dst_ep;
}

static struct zzb_aps_binding *find(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		if (entry->in_use && same(entry, binding)) {
			return entry;
		}
	}

	return NULL;
}

int zzb_apsme_bind_req(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	if (binding->dst_mode != ZZB_APS_ADDR_GROUP && binding->dst_mode != ZZB_APS_ADDR_EXT) {
		return -EINVAL;
	}

	if (find(node, binding) != NULL) {
		return 0;
	}

	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, entry) {
		if (!entry->in_use) {
			*entry = *binding;
			entry->in_use = true;
			return 0;
		}
	}

	return -ENOMEM;
}

int zzb_apsme_unbind_req(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	struct zzb_aps_binding *entry = find(node, binding);

	if (entry == NULL) {
		return -ENOENT;
	}

	entry->in_use = false;
	return 0;
}
