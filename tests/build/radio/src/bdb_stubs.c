/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the BDB functions NWK, APS and ZDO call, until BDB defines them */

#include "aps/aps.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status)
{
}

void zzb_nlme_leave_ind(struct zzb_node *node, uint64_t ext_addr, bool rejoin)
{
}

void zzb_apsme_confirm_key_ind(struct zzb_node *node, uint8_t status, uint64_t src_addr,
			       enum zzb_aps_key_type type)
{
}

void zzb_zdo_node_desc_conf(struct zzb_node *node, uint8_t status, uint16_t addr,
			    uint16_t server_mask)
{
}
