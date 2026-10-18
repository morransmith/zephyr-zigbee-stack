/* SPDX-License-Identifier: Apache-2.0 */

/* Stubs of the NWK functions the MAC calls, until NWK defines them */

#include "mac/mac.h"

void zzb_mcps_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
}

void zzb_mcps_data_ind(struct zzb_node *node, struct net_buf *buf)
{
	net_buf_unref(buf);
}
