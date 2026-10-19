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

/* Secured with the trust center entry and key identifier key_id */
static bool tc_key(const struct zzb_node *node, const struct zzb_aps_rx *rx,
		   enum zzb_sec_key_id key_id)
{
	return (rx->hdr.flags & ZZB_APS_SECURITY) && rx->sec.pair != NULL &&
	       rx->sec.src_addr == node->aps.aib.tc_addr && rx->sec.key_id == key_id;
}

static bool accept_transport_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
				 const struct zzb_aps_cmd *cmd)
{
	switch (cmd->transport_key.type) {
	case ZZB_APS_KEY_NWK:
		return accept_nwk_key(node, rx, cmd);
	case ZZB_APS_KEY_TC_LINK:
		return tc_key(node, rx, ZZB_SEC_KEY_LOAD) && !zzb_nwk_key_pending(node) &&
		       cmd->transport_key.dst == node->eui64 &&
		       cmd->transport_key.src == node->aps.aib.tc_addr;
	default:
		return false;
	}
}

/* R22 4.4.1.3: without APS security the trust center is NWK address 0x0000 */
static bool accept_switch_key(struct zzb_node *node, const struct zzb_aps_rx *rx)
{
	if (node->aps.aib.tc_addr == ZZB_APS_TC_UNKNOWN || zzb_nwk_key_pending(node)) {
		return false;
	}

	return (rx->hdr.flags & ZZB_APS_SECURITY) ? tc_key(node, rx, ZZB_SEC_KEY_DATA)
						  : rx->src_addr == 0x0000;
}

bool zzb_aps_cmd_accept(struct zzb_node *node, const struct zzb_aps_rx *rx,
			const struct zzb_aps_cmd *cmd)
{
	if (cmd->id == ZZB_APS_CMD_TRANSPORT_KEY) {
		return accept_transport_key(node, rx, cmd);
	}

	if (!rx->nwk_security) {
		return false;
	}

	switch (cmd->id) {
	case ZZB_APS_CMD_SWITCH_KEY:
		return accept_switch_key(node, rx);
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
	case ZZB_APS_CMD_SWITCH_KEY:
		zzb_apsme_switch_key_ind(node, node->aps.aib.tc_addr, cmd->switch_key_seq);
		break;
	default:
		break;
	}
}
