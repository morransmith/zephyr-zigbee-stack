/* SPDX-License-Identifier: Apache-2.0 */

#include "aps/aps.h"
#include "common/node.h"
#include "nwk/nwk.h"

/* The source address field of a network key in a distributed security network */
#define DISTRIBUTED_TC UINT64_MAX

/* While the trust center is known, it must be the source (BDB 10.2.2) */
static bool from_tc(const struct zzb_node *node, uint64_t addr)
{
	return node->aps.aib.tc_addr == ZZB_APS_TC_UNKNOWN || addr == node->aps.aib.tc_addr;
}

/* R22 4.4.2.3, 4.4.1.5 */
static bool accept_nwk_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
			   const struct zzb_aps_cmd *cmd)
{
	uint64_t src = cmd->transport_key.src;
	uint64_t dst = cmd->transport_key.dst;

	if (src == DISTRIBUTED_TC || !from_tc(node, src) || (dst != node->eui64 && dst != 0)) {
		return false;
	}

	if (rx->hdr.flags & ZZB_APS_SECURITY) {
		return rx->sec.key_id == ZZB_SEC_KEY_TRANSPORT && rx->sec.src_addr == src;
	}

	return rx->nwk_security && zzb_nwk_is_broadcast(rx->dst_addr) &&
	       !zzb_nwk_key_pending(node) && src == node->aps.aib.tc_addr;
}

bool zzb_aps_cmd_accept(struct zzb_node *node, const struct zzb_aps_rx *rx,
			const struct zzb_aps_cmd *cmd)
{
	switch (cmd->id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		return cmd->transport_key.type == ZZB_APS_KEY_NWK && accept_nwk_key(node, rx, cmd);
	default:
		return false;
	}
}

void zzb_aps_cmd_process(struct zzb_node *node, const struct zzb_aps_rx *rx,
			 const struct zzb_aps_cmd *cmd)
{
	switch (cmd->id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		zzb_apsme_transport_key_ind(node, cmd->transport_key.src, cmd->transport_key.type,
					    cmd->transport_key.key, cmd->transport_key.seq);
		break;
	default:
		break;
	}
}
