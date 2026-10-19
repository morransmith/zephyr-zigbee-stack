/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "aps/aps.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

#define NODE_DESC_LEN 13

/* Logical type (R22 Table 2-30) */
#define LOGICAL_TYPE_END_DEVICE 0x02

/* 2.4 GHz in the frequency band field, bits 3 to 7 of the APS flags octet (R22 Table 2-31) */
#define BAND_2400_MHZ BIT(6)

/* Stack compliance revision 22 in bits 9 to 15, no server role (R22 Table 2-32) */
#define SERVER_MASK (22 << 9)

/* Current power source level 100 % (R22 Table 2-38) */
#define POWER_LEVEL_FULL 0xc

/*
 * Whether the NWKAddrOfInterest of req, its first field, is the node. Another address gets
 * INV_REQUESTTYPE with the address and zeros empty count or length octets (R22 2.4.4.2).
 */
static bool for_node(struct zzb_node *node, const struct zzb_zdo_req *req, size_t zeros)
{
	uint8_t rsp[4] = {ZZB_ZDP_INV_REQUESTTYPE};

	if (sys_get_le16(req->payload) == zzb_nlme_get_short_addr(node)) {
		return true;
	}

	memcpy(rsp + 1, req->payload, 2);
	zzb_zdo_respond(node, req, rsp, 3 + zeros);
	return false;
}

void zzb_zdo_serve_node_desc(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[3 + NODE_DESC_LEN] = {ZZB_ZDP_SUCCESS};
	uint8_t *desc = rsp + 3;

	if (req->len < 2 || !for_node(node, req, 0)) {
		return;
	}

	memcpy(rsp + 1, req->payload, 2);
	desc[0] = LOGICAL_TYPE_END_DEVICE;
	desc[1] = BAND_2400_MHZ;
	desc[2] = zzb_nlme_get_capability(node);
	sys_put_le16(node->device->manufacturer_code, desc + 3);
	/* Without fragmentation the buffer and transfer sizes are those of an ASDU */
	desc[5] = ZZB_APS_MAX_ASDU;
	sys_put_le16(ZZB_APS_MAX_ASDU, desc + 6);
	sys_put_le16(SERVER_MASK, desc + 8);
	sys_put_le16(ZZB_APS_MAX_ASDU, desc + 10);
	zzb_zdo_respond(node, req, rsp, sizeof(rsp));
}

void zzb_zdo_serve_power_desc(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[5] = {ZZB_ZDP_SUCCESS};

	if (req->len < 2 || !for_node(node, req, 0)) {
		return;
	}

	memcpy(rsp + 1, req->payload, 2);
	/* Current power mode 0: the receiver is synchronized with RxOnWhenIdle */
	rsp[3] = node->device->power_source << 4;
	rsp[4] = node->device->power_source | POWER_LEVEL_FULL << 4;
	zzb_zdo_respond(node, req, rsp, sizeof(rsp));
}
