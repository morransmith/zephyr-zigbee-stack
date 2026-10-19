/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_ZCL_ZCL_H_
#define ZZB_SUBSYS_ZCL_ZCL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/net_buf.h>
#include <zzb/zcl.h>

#include "aps/aps.h"
#include "zcl/frame.h"

struct zzb_node;

/* ASDU handle of ZCL frames; their confirm is ignored */
#define ZZB_ZCL_HANDLE 0xfd

/* Global command identifiers (ZCL Table 2-3) */
#define ZZB_ZCL_DEFAULT_RSP 0x0b

/* A received frame for one endpoint */
struct zzb_zcl_req {
	struct zzb_zcl_cmd cmd;
	struct zzb_zcl_hdr hdr;
	/* The profile of the endpoint */
	uint16_t profile;
	/* A NWK unicast to the endpoint, which a Default Response may answer */
	bool unicast;
	/* The instance for the cluster and the direction, or NULL */
	const struct zzb_zcl_cluster *cluster;
};

/*
 * Sends a response to req, a ZCL frame of type with the command identifier cmd and the payload;
 * an error drops it
 */
void zzb_zcl_respond(struct zzb_node *node, const struct zzb_zcl_req *req,
		     enum zzb_zcl_frame_type type, uint8_t cmd, const uint8_t *payload, size_t len);

/* Processes a frame of APSDE-DATA.indication for an endpoint other than 0, and takes buf */
void zzb_zcl_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind);

#endif /* ZZB_SUBSYS_ZCL_ZCL_H_ */
