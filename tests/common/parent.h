/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_COMMON_PARENT_H_
#define ZZB_TESTS_COMMON_PARENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "aps/frame.h"
#include "common/buf.h"
#include "nwk/frame.h"
#include "sec/mmo.h"

#define PAN_ID     0x1a62
#define EXT_PAN_ID 0x00124b0000001a62ULL
#define CHANNEL    15

/* The test keys, also in ci/check-frame-log.sh: network keys 0 and 1, trust center link key B */
extern const uint8_t nwk_key[ZZB_SEC_KEY_LEN];
extern const uint8_t nwk_key_1[ZZB_SEC_KEY_LEN];
extern const uint8_t tc_link_key_b[ZZB_SEC_KEY_LEN];

/* What the parent has received from the child */
struct parent_log {
	int beacon_reqs;
	int polls;
	int ed_timeout_reqs;
	int rejoin_reqs;
	int leaves;
	/* The last NWK command, and the header of the last rejoin request */
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;
	struct zzb_nwk_hdr rejoin_hdr;
	/* The key sequence number of the last frame with NWK security */
	uint8_t nwk_key_seq;
	int aps_data;
	int aps_acks;
	/* The last APS header and command, and the frame counter of the last APS security */
	struct zzb_aps_hdr aps_hdr;
	struct zzb_aps_cmd aps_cmd;
	uint32_t aps_fc;
	/* ZDO frames, and the cluster and NWK destination of the last one */
	int device_annces;
	int node_desc_reqs;
	int permit_joining_reqs;
	uint8_t permit_duration;
	uint8_t tc_significance;
	int mgmt_leave_rsps;
	uint16_t zdo_cluster;
	uint16_t zdo_dst;
	/* Frames of application profiles, and the cluster, endpoints and ASDU of the last one */
	int zcl_frames;
	uint16_t zcl_cluster;
	uint8_t zcl_src_ep;
	uint8_t zcl_dst_ep;
	uint8_t zcl[ZZB_BUF_SIZE];
	size_t zcl_len;
};

extern struct parent_log parent_log;

/*
 * Plays a parent on fake radio 0 with short_addr and eui64. It answers beacon requests,
 * associations with child_addr, End Device Timeout Requests with parent_info, and rejoin
 * requests, and clears parent_log. As the trust center it acknowledges APS data, answers
 * Node_Desc_req with stack compliance revision 22, Request Key with key B and Verify Key with
 * Confirm Key. It does not send the network key on its own.
 */
void parent_start(uint16_t short_addr, uint64_t eui64, uint16_t child_addr, uint8_t parent_info);

/* Sends a leave request to the child, from the stack thread */
void parent_send_leave_request(bool rejoin);

/*
 * Plays the trust center and sends the network key to the child in a Transport Key secured with
 * the key-transport key of the default trust center link key, from the stack thread
 */
void parent_send_transport_key(void);

/* Sets the trust center link key of the child, which parent_start() sets to ZigBeeAlliance09 */
void parent_set_link_key(const uint8_t *key);

/* Sets the stack compliance revision of Node_Desc_rsp */
void parent_set_stack_revision(uint8_t revision);

/*
 * Sends the network key 130 ms after an association or a trust center rejoin, when the child has
 * processed the response
 */
void parent_send_key_after_join(bool send);

/* Sends Mgmt_Leave_req for the child, from the stack thread */
void parent_send_mgmt_leave(bool rejoin);

/* Moves the parent to another channel */
void parent_set_channel(uint8_t channel);

/* Leaves the next count APS data frames without ACK */
void parent_withhold_aps_acks(int count);

/* Sends an APS data frame with ACK request and counter, from the stack thread */
void parent_send_aps_data(uint8_t counter);

/*
 * Sends a ZCL frame of cluster from endpoint 1 to endpoint 1 of the child with profile 0x0104 and
 * an ACK request, from the stack thread
 */
void parent_send_zcl(uint16_t cluster, const uint8_t *asdu, size_t len);

/*
 * Sends a ZCL frame of cluster from endpoint 1 to group with profile 0x0104 as an NWK broadcast to
 * 0xfffd, from the stack thread
 */
void parent_send_group_zcl(uint16_t group, uint16_t cluster, const uint8_t *asdu, size_t len);

/* Sends a ZDO frame of cluster to the child with NWK security only, from the stack thread */
void parent_send_zdo(uint16_t cluster, const uint8_t *asdu, size_t len);

/* Sends network key 1 in a Transport Key, then a broadcast Switch Key, from the stack thread */
void parent_send_nwk_key_update(void);

#endif /* ZZB_TESTS_COMMON_PARENT_H_ */
