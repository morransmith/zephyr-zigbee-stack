/* SPDX-License-Identifier: Apache-2.0 */

#include "aps/aps.h"
#include "common/node.h"

void zzb_aps_init(struct zzb_node *node)
{
	node->aps = (struct zzb_aps){
		.aib.tc_addr = ZZB_APS_TC_UNKNOWN,
	};
}
