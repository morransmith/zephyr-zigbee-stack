/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* NWK_addr_rsp and IEEE_addr_rsp (R22 Tables 2-92, 2-93) */
static void addr_rsp(struct zzb_node *node, const struct zzb_zdo_req *req, uint8_t status,
		     uint64_t ext_addr, uint16_t addr, bool extended)
{
	uint8_t rsp[12] = {status};

	/* R22 2.4.4.1 */
	if (req->broadcast && status != ZZB_ZDP_SUCCESS) {
		return;
	}

	sys_put_le64(ext_addr, rsp + 1);
	sys_put_le16(addr, rsp + 9);
	/* An extended response of an end device has NumAssocDev 0 and no start index */
	zzb_zdo_respond(node, req, rsp, extended ? 12 : 11);
}

static void own_addr_rsp(struct zzb_node *node, const struct zzb_zdo_req *req, uint8_t type)
{
	addr_rsp(node, req, type <= 0x01 ? ZZB_ZDP_SUCCESS : ZZB_ZDP_INV_REQUESTTYPE, node->eui64,
		 zzb_nlme_get_short_addr(node), type == 0x01);
}

/* R22 2.4.3.1.1 */
static void nwk_addr_req(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint64_t ext_addr;

	if (req->len < 10) {
		return;
	}

	ext_addr = sys_get_le64(req->payload);
	if (ext_addr != node->eui64) {
		addr_rsp(node, req, ZZB_ZDP_DEVICE_NOT_FOUND, ext_addr, 0xffff, false);
		return;
	}

	own_addr_rsp(node, req, req->payload[8]);
}

/* R22 2.4.3.1.2 */
static void ieee_addr_req(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint16_t addr;

	if (req->len < 4) {
		return;
	}

	addr = sys_get_le16(req->payload);
	if (addr != zzb_nlme_get_short_addr(node)) {
		addr_rsp(node, req, ZZB_ZDP_DEVICE_NOT_FOUND, UINT64_MAX, addr, false);
		return;
	}

	own_addr_rsp(node, req, req->payload[2]);
}

/* Requests R22 allows only as unicast */
static bool unicast_only(uint16_t cluster)
{
	switch (cluster) {
	case ZZB_ZDP_NODE_DESC_REQ:
	case ZZB_ZDP_POWER_DESC_REQ:
		return true;
	default:
		return false;
	}
}

void zzb_zdo_server(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	const uint8_t status = ZZB_ZDP_NOT_SUPPORTED;

	if (req->broadcast && unicast_only(req->cluster)) {
		return;
	}

	switch (req->cluster) {
	case ZZB_ZDP_NWK_ADDR_REQ:
		nwk_addr_req(node, req);
		return;
	case ZZB_ZDP_IEEE_ADDR_REQ:
		ieee_addr_req(node, req);
		return;
	case ZZB_ZDP_NODE_DESC_REQ:
		zzb_zdo_serve_node_desc(node, req);
		return;
	case ZZB_ZDP_POWER_DESC_REQ:
		zzb_zdo_serve_power_desc(node, req);
		return;
	}

	/* R22 2.4.4.1: no error status answers a broadcast */
	if (!req->broadcast) {
		zzb_zdo_respond(node, req, &status, sizeof(status));
	}
}
