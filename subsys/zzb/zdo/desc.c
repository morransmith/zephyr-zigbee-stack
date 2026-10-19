/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "aps/aps.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

#define NODE_DESC_LEN 13

/* apscMaxDescriptorSize (R22 Table 2-23) */
#define MAX_DESC_LEN 64

/* Logical type (R22 Table 2-30) */
#define LOGICAL_TYPE_END_DEVICE 0x02

/* 2.4 GHz in the frequency band field, bits 3 to 7 of the APS flags octet (R22 Table 2-31) */
#define BAND_2400_MHZ BIT(6)

/* Stack compliance revision 22 in bits 9 to 15, no server role (R22 Table 2-32) */
#define SERVER_MASK (22 << 9)

/* Current power source level 100 % (R22 Table 2-38) */
#define POWER_LEVEL_FULL 0xc

/* Whether the NWKAddrOfInterest of req, its first field, is the node */
static bool for_node(const struct zzb_node *node, const struct zzb_zdo_req *req)
{
	return sys_get_le16(req->payload) == zzb_nlme_get_short_addr(node);
}

/*
 * Answers status with the NWKAddrOfInterest of req and zeros empty count or length octets, as for
 * a request for another device (R22 2.4.4.2)
 */
static void error_rsp(struct zzb_node *node, const struct zzb_zdo_req *req, uint8_t status,
		      size_t zeros)
{
	uint8_t rsp[4] = {status};

	memcpy(rsp + 1, req->payload, 2);
	zzb_zdo_respond(node, req, rsp, 3 + zeros);
}

/* Writes the count and the IDs of the server or the client instances of ep */
static uint8_t *put_clusters(uint8_t *p, const struct zzb_endpoint *ep, bool server)
{
	uint8_t *count = p++;

	*count = 0;
	for (int i = 0; i < ep->cluster_count; i++) {
		if (ep->clusters[i].server != server) {
			continue;
		}

		sys_put_le16(ep->clusters[i].id, p);
		p += 2;
		(*count)++;
	}

	return p;
}

/* Writes the simple descriptor of ep (R22 2.3.2.5) and returns its length */
static size_t put_simple_desc(uint8_t *desc, const struct zzb_endpoint *ep)
{
	uint8_t *p = desc;

	*p++ = ep->id;
	sys_put_le16(ep->profile, p);
	sys_put_le16(ep->device_id, p + 2);
	p[4] = ep->device_version;
	p = put_clusters(p + 5, ep, true);
	p = put_clusters(p, ep, false);
	return p - desc;
}

void zzb_zdo_serve_node_desc(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[3 + NODE_DESC_LEN] = {ZZB_ZDP_SUCCESS};
	uint8_t *desc = rsp + 3;

	if (req->len < 2) {
		return;
	}

	if (!for_node(node, req)) {
		error_rsp(node, req, ZZB_ZDP_INV_REQUESTTYPE, 0);
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

	if (req->len < 2) {
		return;
	}

	if (!for_node(node, req)) {
		error_rsp(node, req, ZZB_ZDP_INV_REQUESTTYPE, 0);
		return;
	}

	memcpy(rsp + 1, req->payload, 2);
	/* Current power mode 0: the receiver is synchronized with RxOnWhenIdle */
	rsp[3] = node->device->power_source << 4;
	rsp[4] = node->device->power_source | POWER_LEVEL_FULL << 4;
	zzb_zdo_respond(node, req, rsp, sizeof(rsp));
}

/* R22 2.4.4.2.5 */
void zzb_zdo_serve_simple_desc(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[4 + MAX_DESC_LEN] = {ZZB_ZDP_SUCCESS};
	const struct zzb_endpoint *ep;
	uint8_t id;

	if (req->len < 3) {
		return;
	}

	id = req->payload[2];
	if (id == 0x00 || id == 0xff) {
		error_rsp(node, req, ZZB_ZDP_INVALID_EP, 1);
		return;
	}

	if (!for_node(node, req)) {
		error_rsp(node, req, ZZB_ZDP_INV_REQUESTTYPE, 1);
		return;
	}

	ep = zzb_node_endpoint(node, id);
	if (ep == NULL) {
		error_rsp(node, req, ZZB_ZDP_NOT_ACTIVE, 1);
		return;
	}

	/* A descriptor beyond apscMaxDescriptorSize gets no response */
	if (8 + 2 * ep->cluster_count > MAX_DESC_LEN) {
		return;
	}

	memcpy(rsp + 1, req->payload, 2);
	rsp[3] = put_simple_desc(rsp + 4, ep);
	zzb_zdo_respond(node, req, rsp, 4 + rsp[3]);
}

/* R22 2.4.4.2.6 */
void zzb_zdo_serve_active_ep(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[ZZB_APS_MAX_ASDU - 1] = {ZZB_ZDP_SUCCESS};
	size_t len = 4;

	if (req->len < 2) {
		return;
	}

	if (!for_node(node, req)) {
		error_rsp(node, req, ZZB_ZDP_INV_REQUESTTYPE, 1);
		return;
	}

	memcpy(rsp + 1, req->payload, 2);
	for (size_t i = 0; i < node->device->endpoint_count && len < sizeof(rsp); i++) {
		rsp[len++] = node->device->endpoints[i].id;
	}

	rsp[3] = len - 4;
	zzb_zdo_respond(node, req, rsp, len);
}

/* Whether one of the count clusters of the request list is a server or a client instance of ep */
static bool any_cluster(const uint8_t *list, uint8_t count, const struct zzb_endpoint *ep,
			bool server)
{
	for (int i = 0; i < count; i++) {
		for (int j = 0; j < ep->cluster_count; j++) {
			const struct zzb_zcl_cluster *cl = &ep->clusters[j];

			if (cl->server == server && cl->id == sys_get_le16(list + 2 * i)) {
				return true;
			}
		}
	}

	return false;
}

/* R22 2.4.4.2.7.1.1 */
static bool matches(const struct zzb_endpoint *ep, uint16_t profile, const uint8_t *in,
		    uint8_t in_count, const uint8_t *out, uint8_t out_count)
{
	if (profile != ep->profile && profile != 0xffff) {
		return false;
	}

	return any_cluster(in, in_count, ep, true) || any_cluster(out, out_count, ep, false);
}

/* R22 2.4.4.2.7 */
void zzb_zdo_serve_match_desc(struct zzb_node *node, const struct zzb_zdo_req *req)
{
	uint8_t rsp[ZZB_APS_MAX_ASDU - 1] = {ZZB_ZDP_SUCCESS};
	const uint8_t *in = req->payload + 5;
	const uint8_t *out;
	uint8_t in_count, out_count;
	uint16_t profile;
	size_t len = 4;

	if (req->len < 6) {
		return;
	}

	in_count = req->payload[4];
	if (req->len < 6 + 2 * in_count) {
		return;
	}

	out_count = in[2 * in_count];
	out = in + 2 * in_count + 1;
	if (req->len < 6 + 2 * (in_count + out_count)) {
		return;
	}

	if (!for_node(node, req) && !zzb_nwk_is_broadcast(sys_get_le16(req->payload))) {
		if (!req->broadcast) {
			error_rsp(node, req, ZZB_ZDP_INV_REQUESTTYPE, 1);
		}

		return;
	}

	profile = sys_get_le16(req->payload + 2);
	for (size_t i = 0; i < node->device->endpoint_count && len < sizeof(rsp); i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		if (matches(ep, profile, in, in_count, out, out_count)) {
			rsp[len++] = ep->id;
		}
	}

	if (len == 4 && req->broadcast) {
		return;
	}

	/* The address of the node also answers a broadcast NWKAddrOfInterest */
	sys_put_le16(zzb_nlme_get_short_addr(node), rsp + 1);
	rsp[3] = len - 4;
	zzb_zdo_respond(node, req, rsp, len);
}
