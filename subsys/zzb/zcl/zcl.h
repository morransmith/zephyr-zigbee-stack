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
#define ZZB_ZCL_READ_ATTRIBUTES         0x00
#define ZZB_ZCL_READ_ATTRIBUTES_RSP     0x01
#define ZZB_ZCL_DEFAULT_RSP             0x0b
#define ZZB_ZCL_DISCOVER_ATTRIBUTES     0x0c
#define ZZB_ZCL_DISCOVER_ATTRIBUTES_RSP 0x0d

/* The longest payload of a response, without the manufacturer code */
#define ZZB_ZCL_MAX_PAYLOAD (ZZB_APS_MAX_ASDU - 3)

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

/* Processes a global command; returns the status of the Default Response or ZZB_ZCL_RESPONDED */
int zzb_zcl_foundation(struct zzb_node *node, const struct zzb_zcl_req *req);

/* The attribute id of cl, or NULL */
const struct zzb_zcl_attr *zzb_zcl_attr_find(const struct zzb_zcl_cluster *cl, uint16_t id);

/* Writes the value of attr of cl in its ZCL encoding; returns its length or -ENOSPC */
int zzb_zcl_attr_encode(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint8_t *p, size_t len);

/* Processes a frame of APSDE-DATA.indication for an endpoint other than 0, and takes buf */
void zzb_zcl_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind);

#endif /* ZZB_SUBSYS_ZCL_ZCL_H_ */
