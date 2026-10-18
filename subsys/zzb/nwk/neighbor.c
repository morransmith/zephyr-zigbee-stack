/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/util.h>

#include "common/node.h"
#include "nwk/nwk.h"

struct zzb_nwk_neighbor *zzb_nwk_neighbor_add(struct zzb_node *node, uint64_t ext_addr,
					      uint16_t short_addr,
					      enum zzb_nwk_relationship relationship)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use) {
			continue;
		}

		*nb = (struct zzb_nwk_neighbor){
			.in_use = true,
			.ext_addr = ext_addr,
			.short_addr = short_addr,
			.relationship = relationship,
		};
		return nb;
	}

	return NULL;
}

void zzb_nwk_neighbor_remove(struct zzb_nwk_neighbor *neighbor)
{
	neighbor->in_use = false;
}

struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_ext(struct zzb_node *node, uint64_t ext_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use && nb->ext_addr == ext_addr) {
			return nb;
		}
	}

	return NULL;
}

struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_short(struct zzb_node *node, uint16_t short_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use && nb->relationship != ZZB_NWK_REL_NONE &&
		    nb->short_addr == short_addr) {
			return nb;
		}
	}

	return NULL;
}

struct zzb_nwk_neighbor *zzb_nwk_neighbor_parent(struct zzb_node *node)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use && nb->relationship == ZZB_NWK_REL_PARENT) {
			return nb;
		}
	}

	return NULL;
}
