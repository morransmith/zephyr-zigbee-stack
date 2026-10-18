/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the ZDO functions NWK calls, until ZDO defines them */

#include "nwk/nwk.h"

void zzb_nlme_network_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count)
{
}

void zzb_nlme_join_conf(struct zzb_node *node, uint8_t status)
{
}

void zzb_nlme_nwk_status_ind(struct zzb_node *node, uint8_t code, uint16_t addr)
{
}

void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status)
{
}

void zzb_nlme_leave_ind(struct zzb_node *node, uint64_t ext_addr, bool rejoin)
{
}
