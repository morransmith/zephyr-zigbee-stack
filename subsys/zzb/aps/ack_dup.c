/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "aps/aps.h"
#include "common/node.h"

/* The time a sender with the same constants retransmits; R22 does not define it */
#define DUP_LIFETIME_MS (ZZB_APS_ACK_WAIT_MS * (ZZB_APS_MAX_FRAME_RETRIES + 1))

bool zzb_aps_dup_check(struct zzb_node *node, uint16_t src_addr, uint8_t counter)
{
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->aps.dups, dup) {
		if (dup->expiry > now && dup->src_addr == src_addr && dup->counter == counter) {
			return true;
		}
	}

	return false;
}

void zzb_aps_dup_add(struct zzb_node *node, uint16_t src_addr, uint8_t counter)
{
	struct zzb_aps_dup *oldest = &node->aps.dups[0];

	ARRAY_FOR_EACH_PTR(node->aps.dups, dup) {
		if (dup->expiry < oldest->expiry) {
			oldest = dup;
		}
	}

	*oldest = (struct zzb_aps_dup){
		.src_addr = src_addr,
		.counter = counter,
		.expiry = k_uptime_get() + DUP_LIFETIME_MS,
	};
}
