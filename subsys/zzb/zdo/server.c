/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>

#include "aps/aps.h"
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

/*
 * Reads the binding of a Bind_req or Unbind_req (R22 2.4.3.2.2). Returns the status of the
 * request, or -EINVAL for a short one.
 */
static int binding_of(struct zzb_node *node, const struct zzb_zdo_req *req,
		      struct zzb_aps_binding *binding)
{
	const uint8_t *p = req->payload;

	if (req->len < 14) {
		return -EINVAL;
	}

	/* The table holds the bindings of the node only */
	if (sys_get_le64(p) != node->eui64) {
		return ZZB_ZDP_NOT_SUPPORTED;
	}

	if (p[8] == 0x00 || p[8] == 0xff) {
		return ZZB_ZDP_INVALID_EP;
	}

	*binding = (struct zzb_aps_binding){
		.src_ep = p[8],
		.cluster = sys_get_le16(p + 9),
		.dst_mode = p[11],
	};

	switch (p[11]) {
	case ZZB_APS_ADDR_GROUP:
		binding->dst_addr = sys_get_le16(p + 12);
		return ZZB_ZDP_SUCCESS;
	case ZZB_APS_ADDR_EXT:
		if (req->len < 21) {
			return -EINVAL;
		}

		binding->dst_ext = sys_get_le64(p + 12);
		binding->dst_ep = p[20];
		return ZZB_ZDP_SUCCESS;
	default:
		/* R22 does not define this case */
		return ZZB_ZDP_NOT_SUPPORTED;
	}
}

static void bind_req(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	struct zzb_aps_binding binding;
	int status = binding_of(node, req, &binding);
	uint8_t rsp = status;

	if (status < 0) {
		return;
	}

	if (status == ZZB_ZDP_SUCCESS && zzb_apsme_bind_req(node, &binding) != 0) {
		rsp = ZZB_ZDP_TABLE_FULL;
	}

	zzb_zdo_respond(node, req, &rsp, sizeof(rsp));
}

/* R22 2.4.3.2.3 */
static void unbind_req(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	struct zzb_aps_binding binding;
	int status = binding_of(node, req, &binding);
	uint8_t rsp = status;

	if (status < 0) {
		return;
	}

	if (status == ZZB_ZDP_SUCCESS && zzb_apsme_unbind_req(node, &binding) != 0) {
		rsp = ZZB_ZDP_NO_ENTRY;
	}

	zzb_zdo_respond(node, req, &rsp, sizeof(rsp));
}

/* Requests R22 allows only as unicast */
static bool unicast_only(uint16_t cluster)
{
	switch (cluster) {
	case ZZB_ZDP_NODE_DESC_REQ:
	case ZZB_ZDP_POWER_DESC_REQ:
	case ZZB_ZDP_SIMPLE_DESC_REQ:
	case ZZB_ZDP_ACTIVE_EP_REQ:
	case ZZB_ZDP_BIND_REQ:
	case ZZB_ZDP_UNBIND_REQ:
	case ZZB_ZDP_MGMT_LQI_REQ:
	case ZZB_ZDP_MGMT_BIND_REQ:
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
	case ZZB_ZDP_SIMPLE_DESC_REQ:
		zzb_zdo_serve_simple_desc(node, req);
		return;
	case ZZB_ZDP_ACTIVE_EP_REQ:
		zzb_zdo_serve_active_ep(node, req);
		return;
	case ZZB_ZDP_MATCH_DESC_REQ:
		zzb_zdo_serve_match_desc(node, req);
		return;
	case ZZB_ZDP_BIND_REQ:
		bind_req(node, req);
		return;
	case ZZB_ZDP_UNBIND_REQ:
		unbind_req(node, req);
		return;
	case ZZB_ZDP_MGMT_LQI_REQ:
		zzb_zdo_serve_mgmt_lqi(node, req);
		return;
	case ZZB_ZDP_MGMT_BIND_REQ:
		zzb_zdo_serve_mgmt_bind(node, req);
		return;
	case ZZB_ZDP_MGMT_PERMIT_JOINING_REQ:
		zzb_zdo_serve_mgmt_permit_joining(node, req);
		return;
	}

	/* R22 2.4.4.1: no error status answers a broadcast */
	if (!req->broadcast) {
		zzb_zdo_respond(node, req, &status, sizeof(status));
	}
}
