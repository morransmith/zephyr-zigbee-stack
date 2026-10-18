/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_NWK_NWK_H_
#define ZZB_SUBSYS_NWK_NWK_H_

#include <stdbool.h>
#include <stdint.h>

struct zzb_node;

enum zzb_nwk_relationship {
	ZZB_NWK_REL_PARENT = 0x00,
	ZZB_NWK_REL_CHILD = 0x01,
	ZZB_NWK_REL_SIBLING = 0x02,
	ZZB_NWK_REL_NONE = 0x03,
	ZZB_NWK_REL_PREVIOUS_CHILD = 0x04,
	ZZB_NWK_REL_UNAUTH_CHILD = 0x05,
};

struct zzb_nwk_neighbor {
	bool in_use;
	uint64_t ext_addr;
	uint16_t short_addr;
	enum zzb_nwk_relationship relationship;
	uint8_t lqi;
	/* Lowest accepted incoming frame counter per key slot (R22 4.3.1.2 step 6) */
	uint32_t in_fc[2];
};

struct zzb_nwk {
	struct zzb_nwk_neighbor neighbors[CONFIG_ZZB_NEIGHBOR_COUNT];
};

/* Adds a neighbor, or returns NULL if the table is full. The caller checks for a duplicate. */
struct zzb_nwk_neighbor *zzb_nwk_neighbor_add(struct zzb_node *node, uint64_t ext_addr,
					      uint16_t short_addr,
					      enum zzb_nwk_relationship relationship);

void zzb_nwk_neighbor_remove(struct zzb_nwk_neighbor *neighbor);

struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_ext(struct zzb_node *node, uint64_t ext_addr);
struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_short(struct zzb_node *node, uint16_t short_addr);
struct zzb_nwk_neighbor *zzb_nwk_neighbor_parent(struct zzb_node *node);

#endif /* ZZB_SUBSYS_NWK_NWK_H_ */
