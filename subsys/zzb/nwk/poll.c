/* SPDX-License-Identifier: Apache-2.0 */

#include "common/node.h"
#include "mac/mac.h"
#include "nwk/nwk.h"

int zzb_nwk_poll(struct zzb_node *node, uint16_t addr)
{
	const struct zzb_mac_addr coord = {
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = node->nwk.nib.pan_id,
		.short_addr = addr,
	};

	return zzb_mlme_poll_req(node, &coord);
}
