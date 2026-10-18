/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>

#include "common/node.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

void zzb_nwk_parent_link_failure(struct zzb_node *node)
{
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	node->nwk.parent_failures = 0;
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE,
				parent ? parent->short_addr : 0xffff);
}

/* On an end device every frame and poll goes to the parent */
void zzb_nwk_parent_tx_status(struct zzb_node *node, enum zzb_mac_status status)
{
	struct zzb_nwk *nwk = &node->nwk;

	if (nwk->op != ZZB_NWK_OP_NONE || zzb_nwk_neighbor_parent(node) == NULL) {
		return;
	}

	if (status == ZZB_MAC_STATUS_SUCCESS || status == ZZB_MAC_STATUS_NO_DATA) {
		nwk->parent_failures = 0;
		return;
	}

	/* A channel access failure means a busy channel */
	if (status == ZZB_MAC_STATUS_NO_ACK &&
	    ++nwk->parent_failures >= CONFIG_ZZB_NWK_PARENT_FAILURES) {
		zzb_nwk_parent_link_failure(node);
	}
}

/* R22 3.6.1.9.3, without jitter: the parent applies it when it relays the broadcast */
static void send_conflict(struct zzb_node *node, uint16_t addr)
{
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);
	struct zzb_nwk_hdr hdr = {
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE,
		.dst = 0xfffd,
		.radius = ZZB_NWK_DEFAULT_RADIUS,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_NWK_STATUS,
		.nwk_status = {.code = ZZB_NWK_CODE_ADDR_CONFLICT, .has_addr = true, .addr = addr},
	};

	if (parent != NULL) {
		zzb_nwk_send_cmd(node, &hdr, &cmd, parent->short_addr, false);
	}
}

bool zzb_nwk_addr_conflict(struct zzb_node *node, const struct zzb_nwk_hdr *hdr)
{
	const struct zzb_nwk *nwk = &node->nwk;
	uint16_t addr = nwk->nib.short_addr;

	if (nwk->nib.ext_pan_id == 0 || nwk->op != ZZB_NWK_OP_NONE || hdr->dst != addr ||
	    !(hdr->flags & ZZB_NWK_DST_IEEE) || hdr->dst_ext == node->eui64) {
		return false;
	}

	/* BDB rejoins to obtain a new address */
	send_conflict(node, addr);
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_ADDR_CONFLICT, addr);
	return true;
}

void zzb_nwk_status_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
		       const struct zzb_nwk_cmd *cmd)
{
	uint16_t addr = cmd->nwk_status.has_addr ? cmd->nwk_status.addr : hdr->src;

	zzb_nlme_nwk_status_ind(node, cmd->nwk_status.code, addr);
}
