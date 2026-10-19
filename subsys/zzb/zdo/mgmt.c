/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>

#include "aps/aps.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

#define LQI_RECORD_LEN 22

/* RxOnWhenIdle and permit joining of a Mgmt_Lqi_rsp record (R22 Table 2-130) */
#define LQI_ON      1
#define LQI_UNKNOWN 2

static void put_lqi_record(struct zzb_node *node, const struct zzb_nwk_neighbor *nb, uint8_t *p)
{
	uint8_t rx_on = nb->device_type == ZZB_NWK_DEVICE_END_DEVICE ? LQI_UNKNOWN : LQI_ON;

	sys_put_le64(zzb_nlme_get_ext_pan_id(node), p);
	sys_put_le64(nb->ext_addr != 0 ? nb->ext_addr : UINT64_MAX, p + 8);
	sys_put_le16(nb->short_addr, p + 16);
	p[18] = nb->device_type | rx_on << 2 | nb->relationship << 4;
	p[19] = LQI_UNKNOWN;
	/* 0 for a router parent after a reboot: the NWK record does not keep it */
	p[20] = nb->beacon.depth;
	p[21] = nb->lqi;
}

/* R22 2.4.3.3.2, 2.4.4.4.2: the entries with a relationship, as many as fit from the start index */
void zzb_zdo_serve_mgmt_lqi(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[ZZB_APS_MAX_ASDU - 1] = {ZZB_ZDP_SUCCESS};
	uint8_t entries = 0;
	size_t len = 4;

	if (req->len < 1) {
		return;
	}

	rsp[2] = req->payload[0];
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (!nb->in_use || nb->relationship == ZZB_NWK_REL_NONE) {
			continue;
		}

		if (++entries <= rsp[2] || len + LQI_RECORD_LEN > sizeof(rsp)) {
			continue;
		}

		put_lqi_record(node, nb, rsp + len);
		len += LQI_RECORD_LEN;
		rsp[3]++;
	}

	rsp[1] = entries;
	zzb_zdo_respond(node, req, rsp, len);
}

static size_t binding_record_len(const struct zzb_aps_binding *binding)
{
	return binding->dst_mode == ZZB_APS_ADDR_GROUP ? 14 : 21;
}

static void put_binding_record(struct zzb_node *node, const struct zzb_aps_binding *binding,
			       uint8_t *p)
{
	sys_put_le64(node->eui64, p);
	p[8] = binding->src_ep;
	sys_put_le16(binding->cluster, p + 9);
	p[11] = binding->dst_mode;
	if (binding->dst_mode == ZZB_APS_ADDR_GROUP) {
		sys_put_le16(binding->dst_addr, p + 12);
		return;
	}

	sys_put_le64(binding->dst_ext, p + 12);
	p[20] = binding->dst_ep;
}

/* R22 2.4.3.3.4, 2.4.4.4.4 */
void zzb_zdo_serve_mgmt_bind(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[ZZB_APS_MAX_ASDU - 1] = {ZZB_ZDP_SUCCESS};
	uint8_t entries = 0;
	bool full = false;
	size_t len = 4;

	if (req->len < 1) {
		return;
	}

	rsp[2] = req->payload[0];
	ARRAY_FOR_EACH_PTR(node->aps.aib.bindings, binding) {
		if (!binding->in_use) {
			continue;
		}

		if (++entries <= rsp[2] || full) {
			continue;
		}

		/* The records of a response are consecutive entries */
		if (len + binding_record_len(binding) > sizeof(rsp)) {
			full = true;
			continue;
		}

		put_binding_record(node, binding, rsp + len);
		len += binding_record_len(binding);
		rsp[3]++;
	}

	rsp[1] = entries;
	zzb_zdo_respond(node, req, rsp, len);
}

/* R22 2.4.4.4.7 with the status of NLME-PERMIT-JOINING.confirm of an end device (3.2.2.7.3) */
void zzb_zdo_serve_mgmt_permit_joining(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	const uint8_t rsp = ZZB_NWK_STATUS_INVALID_REQUEST;

	if (req->len < 2 || req->broadcast) {
		return;
	}

	zzb_zdo_respond(node, req, &rsp, sizeof(rsp));
}

void zzb_zdo_leave(struct zzb_node *node)
{
	if (!node->zdo.leave_pending) {
		return;
	}

	node->zdo.leave_pending = false;
	zzb_nlme_leave_req(node, node->zdo.leave_rejoin);
}

/*
 * R22 2.4.3.3.5, 3.6.1.10.3.1: the response goes out before the node leaves. The rejoin flag comes
 * from the request, as in R23; Remove Children has no effect on an end device.
 */
void zzb_zdo_serve_mgmt_leave(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint64_t addr;
	uint8_t status;

	if (req->len < 9) {
		return;
	}

	addr = sys_get_le64(req->payload);
	if (addr != node->eui64 && addr != 0) {
		status = ZZB_NWK_STATUS_UNKNOWN_DEVICE;
		zzb_zdo_respond(node, req, &status, sizeof(status));
		return;
	}

	status = ZZB_ZDP_SUCCESS;
	node->zdo.leave_rejoin = req->payload[8] & BIT(7);
	node->zdo.leave_pending = true;
	if (zzb_zdo_send(node, req->src_addr, req->cluster | ZZB_ZDP_RSP, req->tsn, &status,
			 sizeof(status), ZZB_ZDO_LEAVE_HANDLE) != 0) {
		zzb_zdo_leave(node);
	}
}
