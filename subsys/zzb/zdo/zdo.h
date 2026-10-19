/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_ZDO_ZDO_H_
#define ZZB_SUBSYS_ZDO_ZDO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util_macro.h>

struct zzb_node;

/* The cluster of a response is the cluster of its request with bit 15 set (R22 2.4.4.1) */
#define ZZB_ZDP_RSP BIT(15)

/* ZDP clusters (R22 Tables 2-44, 2-68, 2-80) */
enum zzb_zdp_cluster {
	ZZB_ZDP_NWK_ADDR_REQ = 0x0000,
	ZZB_ZDP_IEEE_ADDR_REQ = 0x0001,
	ZZB_ZDP_NODE_DESC_REQ = 0x0002,
	ZZB_ZDP_POWER_DESC_REQ = 0x0003,
	ZZB_ZDP_SIMPLE_DESC_REQ = 0x0004,
	ZZB_ZDP_ACTIVE_EP_REQ = 0x0005,
	ZZB_ZDP_MATCH_DESC_REQ = 0x0006,
	ZZB_ZDP_BIND_REQ = 0x0021,
	ZZB_ZDP_UNBIND_REQ = 0x0022,
	ZZB_ZDP_MGMT_LQI_REQ = 0x0031,
	ZZB_ZDP_MGMT_BIND_REQ = 0x0033,
	ZZB_ZDP_MGMT_LEAVE_REQ = 0x0034,
	ZZB_ZDP_MGMT_PERMIT_JOINING_REQ = 0x0036,
};

/* ZDP status values (R22 Table 2-145) */
enum zzb_zdp_status {
	ZZB_ZDP_SUCCESS = 0x00,
	ZZB_ZDP_INV_REQUESTTYPE = 0x80,
	ZZB_ZDP_DEVICE_NOT_FOUND = 0x81,
	ZZB_ZDP_INVALID_EP = 0x82,
	ZZB_ZDP_NOT_ACTIVE = 0x83,
	ZZB_ZDP_NOT_SUPPORTED = 0x84,
	ZZB_ZDP_NO_ENTRY = 0x88,
	ZZB_ZDP_TABLE_FULL = 0x8c,
};

/* ASDU handles of ZDO frames: Mgmt_Leave_rsp, whose confirm starts the leave, and the others */
#define ZZB_ZDO_LEAVE_HANDLE 0xfe
#define ZZB_ZDO_HANDLE       0xff

struct zzb_zdo {
	/* Transaction sequence number of the next frame ZDO originates */
	uint8_t tsn;
	/* Mgmt_Leave_rsp waits for its confirm */
	bool leave_pending;
	/* The Rejoin flag of the last Mgmt_Leave_req for the node, which BDB reads */
	bool leave_rejoin;
};

/* A received request */
struct zzb_zdo_req {
	uint16_t cluster;
	uint16_t src_addr;
	bool broadcast;
	uint8_t tsn;
	const uint8_t *payload;
	size_t len;
};

void zzb_zdo_init(struct zzb_node *node);

/*
 * Sends a ZDO frame of tsn and the payload to the network address dst, with an APS ACK request
 * when dst is not a broadcast address. Returns -ENOBUFS without a buffer or an error of
 * APSDE-DATA.request.
 */
int zzb_zdo_send(struct zzb_node *node, uint16_t dst, uint16_t cluster, uint8_t tsn,
		 const uint8_t *payload, size_t len, uint8_t handle);

/* Sends the response of req with the payload; an error drops it */
void zzb_zdo_respond(struct zzb_node *node, const struct zzb_zdo_req *req, const uint8_t *payload,
		     size_t len);

/* Processes a request */
void zzb_zdo_server(struct zzb_node *node, const struct zzb_zdo_req *req);

/* Server of the descriptor requests */
void zzb_zdo_serve_node_desc(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_power_desc(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_simple_desc(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_active_ep(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_match_desc(struct zzb_node *node, const struct zzb_zdo_req *req);

/* Server of the management requests */
void zzb_zdo_serve_mgmt_lqi(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_mgmt_bind(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_mgmt_permit_joining(struct zzb_node *node, const struct zzb_zdo_req *req);
void zzb_zdo_serve_mgmt_leave(struct zzb_node *node, const struct zzb_zdo_req *req);

/* Leaves the network after the confirm of Mgmt_Leave_rsp */
void zzb_zdo_leave(struct zzb_node *node);

#endif /* ZZB_SUBSYS_ZDO_ZDO_H_ */
