/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the ZDO functions NWK and APS call, until ZDO defines them */

#include "aps/aps.h"
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

void zzb_apsde_data_conf(struct zzb_node *node, uint8_t handle, uint8_t status)
{
}

void zzb_apsde_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind)
{
	net_buf_unref(buf);
}

void zzb_apsme_transport_key_ind(struct zzb_node *node, uint64_t src_addr,
				 enum zzb_aps_key_type type, const uint8_t *key, uint8_t seq)
{
}
