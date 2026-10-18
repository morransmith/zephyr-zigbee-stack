/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the APS functions NWK calls, until APS defines them */

#include "nwk/nwk.h"

void zzb_nlde_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
}

void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use)
{
	net_buf_unref(buf);
}
