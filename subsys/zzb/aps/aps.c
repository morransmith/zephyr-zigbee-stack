/* SPDX-License-Identifier: Apache-2.0 */

#include "aps/aps.h"
#include "common/node.h"

void zzb_aps_init(struct zzb_node *node)
{
	node->aps = (struct zzb_aps){
		.aib.tc_addr = ZZB_APS_TC_UNKNOWN,
	};
	zzb_fc_init(&node->aps.fc, node, "fc/aps");
}

void zzb_aps_reset(struct zzb_node *node)
{
	node->aps.aib = (struct zzb_aib){
		.tc_addr = ZZB_APS_TC_UNKNOWN,
	};
	zzb_aps_delete(node);
}
