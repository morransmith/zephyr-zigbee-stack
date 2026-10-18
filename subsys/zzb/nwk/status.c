/* SPDX-License-Identifier: Apache-2.0 */

#include "common/node.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

void zzb_nwk_parent_link_failure(struct zzb_node *node)
{
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE,
				parent ? parent->short_addr : 0xffff);
}

void zzb_nwk_status_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
		       const struct zzb_nwk_cmd *cmd)
{
	uint16_t addr = cmd->nwk_status.has_addr ? cmd->nwk_status.addr : hdr->src;

	zzb_nlme_nwk_status_ind(node, cmd->nwk_status.code, addr);
}
