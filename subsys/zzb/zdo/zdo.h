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

/* ZDP status values (R22 Table 2-145) */
enum zzb_zdp_status {
	ZZB_ZDP_SUCCESS = 0x00,
	ZZB_ZDP_NOT_SUPPORTED = 0x84,
};

/* ASDU handle of the ZDO frames; their confirm is ignored */
#define ZZB_ZDO_HANDLE 0xff

struct zzb_zdo {
	/* Transaction sequence number of the next frame ZDO originates */
	uint8_t tsn;
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

#endif /* ZZB_SUBSYS_ZDO_ZDO_H_ */
