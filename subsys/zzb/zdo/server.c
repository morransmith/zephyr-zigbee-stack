/* SPDX-License-Identifier: Apache-2.0 */

#include "common/node.h"
#include "zdo/zdo.h"

void zzb_zdo_server(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	const uint8_t status = ZZB_ZDP_NOT_SUPPORTED;

	/* R22 2.4.4.1: no error status answers a broadcast */
	if (!req->broadcast) {
		zzb_zdo_respond(node, req, &status, sizeof(status));
	}
}
