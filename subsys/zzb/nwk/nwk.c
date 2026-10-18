/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/random/random.h>

#include "common/node.h"
#include "nwk/nwk.h"

void zzb_nwk_init(struct zzb_node *node)
{
	node->nwk.nib = (struct zzb_nib){
		.pan_id = 0xffff,
		.short_addr = 0xffff,
		.seq = sys_rand8_get(),
	};
}
