/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_NWK_NWK_H_
#define ZZB_SUBSYS_NWK_NWK_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>

#include "common/fc.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "sec/mmo.h"

struct zzb_node;

/* Auxiliary header of a NWK frame: security control, frame counter, source address, key sequence */
#define ZZB_NWK_AUX_LEN 14

/*
 * Headroom of NLDE-DATA.request: MAC header of a NWK frame, NWK header of a data frame, auxiliary
 * header
 */
#define ZZB_NLDE_HEADROOM (ZZB_MAC_DATA_HDR_LEN + 8 + ZZB_NWK_AUX_LEN)

/* MSDU handles from 0xf0 belong to NWK command frames: 0xf0 | command identifier */
#define ZZB_NWK_CMD_HANDLE 0xf0

/* Default radius: 2 * nwkMaxDepth (15 for the Zigbee PRO stack profile) */
#define ZZB_NWK_DEFAULT_RADIUS 30

/* Capability Information (R22 Table 3-62) */
#define ZZB_NWK_CAP_ROUTER     BIT(1)
#define ZZB_NWK_CAP_RX_ON_IDLE BIT(3)
#define ZZB_NWK_CAP_ALLOC_ADDR BIT(7)

/* nwkParentInformation (R22 Table 3-55) */
#define ZZB_NWK_PARENT_POLL_KEEPALIVE       BIT(0)
#define ZZB_NWK_PARENT_ED_TIMEOUT_KEEPALIVE BIT(1)

/* Broadcast addresses (R22 Table 3-69) */
static inline bool zzb_nwk_is_broadcast(uint16_t addr)
{
	return addr == 0xffff || addr == 0xfffd || addr == 0xfffc || addr == 0xfffb;
}

/* NWK layer status values (R22 Table 3-73) */
enum zzb_nwk_status {
	ZZB_NWK_STATUS_SUCCESS = 0x00,
	ZZB_NWK_STATUS_INVALID_REQUEST = 0xc2,
	ZZB_NWK_STATUS_NOT_PERMITTED = 0xc3,
	ZZB_NWK_STATUS_UNKNOWN_DEVICE = 0xc8,
	ZZB_NWK_STATUS_NO_NETWORKS = 0xca,
};

/* Status codes of the network status command and NLME-NWK-STATUS.indication (R22 Table 3-51) */
enum zzb_nwk_status_code {
	ZZB_NWK_CODE_PARENT_LINK_FAILURE = 0x09,
	ZZB_NWK_CODE_ADDR_CONFLICT = 0x0d,
	ZZB_NWK_CODE_ADDR_UPDATE = 0x10,
};

/* Entry of nwkSecurityMaterialSet */
struct zzb_nwk_key {
	bool valid;
	uint8_t seq;
	uint8_t key[ZZB_SEC_KEY_LEN];
};

struct zzb_nib {
	uint16_t pan_id;
	uint16_t short_addr;
	uint64_t ext_pan_id;
	uint8_t seq;
	uint8_t update_id;
	uint8_t capability;
	uint8_t parent_info;
	/* The channel of the network, as in nwkMacInterfaceTable */
	uint8_t channel;
	struct zzb_nwk_key keys[2];
	/* The slot of nwkActiveKeySeqNumber and the slot written last */
	uint8_t active_key;
	uint8_t newest_key;
};

enum zzb_nwk_relationship {
	ZZB_NWK_REL_PARENT = 0x00,
	ZZB_NWK_REL_CHILD = 0x01,
	ZZB_NWK_REL_SIBLING = 0x02,
	ZZB_NWK_REL_NONE = 0x03,
	ZZB_NWK_REL_PREVIOUS_CHILD = 0x04,
	ZZB_NWK_REL_UNAUTH_CHILD = 0x05,
};

enum zzb_nwk_device_type {
	ZZB_NWK_DEVICE_COORDINATOR = 0x00,
	ZZB_NWK_DEVICE_ROUTER = 0x01,
	ZZB_NWK_DEVICE_END_DEVICE = 0x02,
};

struct zzb_nwk_neighbor {
	bool in_use;
	uint64_t ext_addr;
	uint16_t short_addr;
	enum zzb_nwk_device_type device_type;
	enum zzb_nwk_relationship relationship;
	uint8_t lqi;
	/* Lowest accepted incoming frame counter per key slot (R22 4.3.1.2 step 6) */
	uint32_t in_fc[2];
	/* Network discovery fields (R22 Table 3-64), from the beacon of a router */
	struct zzb_nwk_beacon beacon;
	uint16_t pan_id;
	uint8_t channel;
	bool permit_join;
	bool potential_parent;
};

/* Network descriptor of NLME-NETWORK-DISCOVERY.confirm (R22 Table 3-12) */
struct zzb_nwk_network {
	uint64_t ext_pan_id;
	uint16_t pan_id;
	uint8_t channel;
	uint8_t update_id;
	/* ORed over the routers of the network */
	bool permit_join;
	bool router_capacity;
	bool ed_capacity;
};

/* The NLME procedure in progress */
enum zzb_nwk_op {
	ZZB_NWK_OP_NONE,
	ZZB_NWK_OP_DISCOVERY,
	ZZB_NWK_OP_JOIN,
	ZZB_NWK_OP_REJOIN,
	ZZB_NWK_OP_LEAVE,
};

/* RejoinNetwork parameter of NLME-JOIN.request */
enum zzb_nwk_join_method {
	ZZB_NWK_JOIN_ASSOC = 0x00,
	ZZB_NWK_JOIN_REJOIN = 0x02,
};

struct zzb_nwk {
	struct zzb_nib nib;
	struct zzb_fc fc;
	struct zzb_nwk_neighbor neighbors[CONFIG_ZZB_NEIGHBOR_COUNT];
	struct zzb_nwk_network networks[CONFIG_ZZB_NWK_NETWORK_COUNT];
	size_t network_count;
	enum zzb_nwk_op op;
	/* The network to join, the parent tried and whether a rejoin is secured */
	uint64_t join_ext_pan_id;
	struct zzb_nwk_neighbor *candidate;
	bool join_secure;
	/* Waits for the rejoin response */
	struct k_work_delayable timer;
	/* PAN ID, network address and channel before a rejoin, restored when it fails */
	uint16_t prev_pan_id;
	uint16_t prev_short_addr;
	uint8_t prev_channel;
	/* The leave in progress: its rejoin flag, and whether the parent requested it */
	bool leave_rejoin;
	bool leave_requested;
	/* Set by an association join or a trust center rejoin until NLME-SET of a network key */
	bool key_pending;
	/* Sends the End Device Timeout Request and the keepalives */
	struct k_work_delayable keepalive;
	bool ed_timeout_due;
	/* A keepalive End Device Timeout Request waits for its response */
	bool ed_timeout_wait;
	/* Consecutive transmissions to the parent without an ACK */
	uint8_t parent_failures;
};

/* Clears the NWK state. The NIB has no PAN ID, no network address and a random sequence number. */
void zzb_nwk_init(struct zzb_node *node);

/* NLME-SET of nwkPANId and nwkNetworkAddress, which also set macPANId and macShortAddress */
void zzb_nlme_set_pan_id(struct zzb_node *node, uint16_t pan_id);
void zzb_nlme_set_short_addr(struct zzb_node *node, uint16_t short_addr);

void zzb_nlme_set_ext_pan_id(struct zzb_node *node, uint64_t ext_pan_id);

/*
 * NLME-SET of a network key in nwkSecurityMaterialSet. The key replaces the key with the same
 * sequence number, otherwise the key that is not active, and the incoming frame counters of its
 * slot are reset. The first key becomes the active key. A pending key ends, and the End Device
 * Timeout Request follows.
 */
void zzb_nlme_set_nwk_key(struct zzb_node *node, const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t seq);

/*
 * NLME-SET of nwkActiveKeySeqNumber: the key with sequence number seq becomes active. Returns
 * -ENOENT without such a key.
 */
int zzb_nlme_set_active_key(struct zzb_node *node, uint8_t seq);

/*
 * NLME-SET of the channel of nwkMacInterfaceTable and of nwkUpdateId. MLME-SET of the channel
 * comes first, and the NIB and the records change only when it succeeds. Returns -EINVAL for a
 * channel outside 11 to 26, -EBUSY while an NLME procedure runs, or an error of MLME-SET.
 */
int zzb_nlme_set_channel(struct zzb_node *node, uint8_t channel, uint8_t update_id);

/* NLME-GET of nwkNetworkAddress, nwkExtendedPANID and nwkCapabilityInformation */
uint16_t zzb_nlme_get_short_addr(const struct zzb_node *node);
uint64_t zzb_nlme_get_ext_pan_id(const struct zzb_node *node);
uint8_t zzb_nlme_get_capability(const struct zzb_node *node);

/* Whether the network key is pending: joined and unauthorized (R22 4.4.1.2 step 2) */
bool zzb_nwk_key_pending(const struct zzb_node *node);

/*
 * Saves the records nwk/net and nwk/keys while the node is on a network and its key is not
 * pending. Errors are ignored; the next save writes the whole records again.
 */
void zzb_nwk_save(struct zzb_node *node);

/* Deletes the records nwk/net and nwk/keys */
void zzb_nwk_delete(struct zzb_node *node);

/* Load the records nwk/net and nwk/keys; -EINVAL for an unknown version or length */
int zzb_nwk_net_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg);
int zzb_nwk_keys_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg);

/*
 * NLDE-DATA.request. buf holds the NSDU after ZZB_NLDE_HEADROOM bytes of headroom, and its
 * metadata the NSDU handle. Radius 0 selects ZZB_NWK_DEFAULT_RADIUS. The frame goes to the
 * neighbor with the network address dst_addr, otherwise to the parent. Returns -EINVAL for a
 * reserved destination or a handle from ZZB_NWK_CMD_HANDLE, -EBUSY while an NLME procedure runs,
 * -ENOTCONN without a network address or a parent, -EMSGSIZE without the headroom, or an error of
 * zzb_nwk_secure() or MCPS-DATA.request.
 */
int zzb_nlde_data_req(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr, uint8_t radius,
		      bool secure);

/* Defined by APS. status is the status of MCPS-DATA.confirm. */
void zzb_nlde_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status);

/* Defined by APS. buf holds the NSDU, and its metadata the LQI. */
void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use);

/*
 * Secures a frame in place (R22 4.3.1.1). buf holds the NWK header of hdr_len bytes with
 * ZZB_NWK_SECURITY set, then the payload, and has ZZB_NWK_AUX_LEN bytes of headroom and
 * ZZB_SEC_MIC_LEN bytes of tailroom. Returns -ENOENT without an active key, -EMSGSIZE without
 * the headroom or tailroom, or an error of zzb_fc_next().
 */
int zzb_nwk_secure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len);

/*
 * Verifies and decrypts a frame in place (R22 4.3.1.2), then removes the auxiliary header and the
 * MIC. Returns -EINVAL for a malformed auxiliary header or one without the network key identifier
 * and extended nonce, -EALREADY for a stale frame counter, -ENOENT if the key or the sender is not
 * known, or -EBADMSG if the MIC does not match.
 */
int zzb_nwk_unsecure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len);

/*
 * NLME-NETWORK-DISCOVERY.request: an active scan of channels, bit n selecting channel n, for
 * aBaseSuperframeDuration * (2^duration + 1) symbols per channel. Returns -EBUSY while an NLME
 * procedure runs, or an error of MLME-SCAN.request.
 */
int zzb_nlme_network_discovery_req(struct zzb_node *node, uint32_t channels, uint8_t duration);

/*
 * Defined by BDB. status is the status of MLME-SCAN.confirm. networks stay valid until the next
 * discovery.
 */
void zzb_nlme_network_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count);

/*
 * NLME-JOIN.request. An association uses the neighbor entries of the last discovery and ignores
 * channels and duration. A rejoin scans channels, or the channel of the network when channels is
 * 0. Returns -EINVAL for extended PAN ID 0, -ENOTSUP for another method, the router device type or
 * a receiver off when idle, -EBUSY while an NLME procedure runs, -EALREADY for an association on
 * a network, -ENETUNREACH for an association without a suitable parent, -ENOENT for a secure
 * rejoin without a network key, or an error of MLME-ASSOCIATE.request or MLME-SCAN.request.
 */
int zzb_nlme_join_req(struct zzb_node *node, uint64_t ext_pan_id, enum zzb_nwk_join_method method,
		      uint32_t channels, uint8_t duration, uint8_t capability, bool secure);

/*
 * Defined by BDB. status is SUCCESS, the status of the last MLME-ASSOCIATE.confirm of an
 * association, or NO_NETWORKS or NOT_PERMITTED of a rejoin.
 */
void zzb_nlme_join_conf(struct zzb_node *node, uint8_t status);

/*
 * NLME-LEAVE.request of an end device, which removes itself and has no children. Returns -EBUSY
 * while an NLME procedure runs, -ENOTCONN off a network or without a parent, or an error of
 * zzb_nwk_send_cmd(), -ENOENT without a network key.
 */
int zzb_nlme_leave_req(struct zzb_node *node, bool rejoin);

/* Defined by BDB. status is the status of MCPS-DATA.confirm of the leave command. */
void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status);

/*
 * Defined by BDB. NLME-LEAVE.indication: the neighbor ext_addr has left, or the node has left
 * when ext_addr is 0.
 */
void zzb_nlme_leave_ind(struct zzb_node *node, uint64_t ext_addr, bool rejoin);

/*
 * NLME-RESET.request with WarmStart FALSE: clears the NIB and the tables except the outgoing frame
 * counter and nwkSequenceNumber. Returns -EBUSY while an NLME procedure runs.
 */
int zzb_nlme_reset_req(struct zzb_node *node);

/*
 * Defined by BDB. NLME-NWK-STATUS.indication with a status code of R22 Table 3-51 and the network
 * address it concerns.
 */
void zzb_nlme_nwk_status_ind(struct zzb_node *node, uint8_t code, uint16_t addr);

/* Adds a neighbor, or returns NULL if the table is full. The caller checks for a duplicate. */
struct zzb_nwk_neighbor *zzb_nwk_neighbor_add(struct zzb_node *node, uint64_t ext_addr,
					      uint16_t short_addr,
					      enum zzb_nwk_relationship relationship);

void zzb_nwk_neighbor_remove(struct zzb_nwk_neighbor *neighbor);

struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_ext(struct zzb_node *node, uint64_t ext_addr);

/* Skips the entries without relationship, which come from beacons */
struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_short(struct zzb_node *node, uint16_t short_addr);
struct zzb_nwk_neighbor *zzb_nwk_neighbor_parent(struct zzb_node *node);

/* Within NWK */

/* The slot of the valid network key with sequence number seq, or -ENOENT */
int zzb_nwk_key_slot(const struct zzb_nib *nib, uint8_t seq);

/*
 * Completes hdr with the source addresses, the sequence number and the end device initiator bit
 * and sends it with the payload in buf to the neighbor mac_dst. buf has ZZB_MAC_DATA_HDR_LEN bytes
 * of headroom for the MAC header, then room for the NWK and auxiliary headers. On error buf stays
 * with the caller.
 */
int zzb_nwk_send(struct zzb_node *node, struct net_buf *buf, struct zzb_nwk_hdr *hdr,
		 uint16_t mac_dst, bool ack);

/* Sends cmd as a command frame with hdr; see zzb_nwk_send(). Returns -ENOBUFS without a buffer. */
int zzb_nwk_send_cmd(struct zzb_node *node, struct zzb_nwk_hdr *hdr, const struct zzb_nwk_cmd *cmd,
		     uint16_t mac_dst, bool ack);

/* Removes the entries without relationship; the beacons of the scan mark the potential parents */
void zzb_nwk_start_scan(struct zzb_node *node);

/* Starts a join or rejoin of ext_pan_id: nwkParentInformation 0, no keepalive */
void zzb_nwk_start_join(struct zzb_node *node, uint64_t ext_pan_id, uint8_t capability);

/* The suitable parent with the highest LQI, or NULL */
struct zzb_nwk_neighbor *zzb_nwk_select_parent(struct zzb_node *node, bool rejoin);

/* Makes parent the only neighbor entry; the NIB takes its network */
void zzb_nwk_become_child(struct zzb_node *node, struct zzb_nwk_neighbor *parent);

void zzb_nwk_rejoin_init(struct zzb_node *node);
int zzb_nwk_rejoin_req(struct zzb_node *node, uint64_t ext_pan_id, uint32_t channels,
		       uint8_t duration, uint8_t capability, bool secure);
void zzb_nwk_rejoin_scan_conf(struct zzb_node *node, enum zzb_mac_status status);
void zzb_nwk_rejoin_req_conf(struct zzb_node *node, enum zzb_mac_status status);
void zzb_nwk_rejoin_rsp_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
			   const struct zzb_nwk_cmd *cmd, uint16_t mac_src);

/* Clears the NWK state as NLME-RESET.request does */
void zzb_nwk_reset(struct zzb_node *node);

void zzb_nwk_leave_conf(struct zzb_node *node, enum zzb_mac_status status);

/* Handles a received leave command from the MAC source mac_src */
void zzb_nwk_leave_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
		      const struct zzb_nwk_cmd *cmd, uint16_t mac_src);

void zzb_nwk_ed_timeout_init(struct zzb_node *node);

/* Sends the End Device Timeout Request from the stack queue, again after 1 s if that fails */
void zzb_nwk_ed_timeout_start(struct zzb_node *node);

/* Starts the keepalive of a node loaded from its records (R22 3.6.10.8) */
void zzb_nwk_keepalive_resume(struct zzb_node *node);

/* Cancels the End Device Timeout Request and the keepalive */
void zzb_nwk_keepalive_stop(struct zzb_node *node);

/* Handles a received End Device Timeout Response from the MAC source mac_src */
void zzb_nwk_ed_timeout_rsp_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
			       const struct zzb_nwk_cmd *cmd, uint16_t mac_src);

/* NLME-NWK-STATUS.indication with parent link failure; the failure count starts again */
void zzb_nwk_parent_link_failure(struct zzb_node *node);

/* Counts a transmission to the parent with the status of its confirm */
void zzb_nwk_parent_tx_status(struct zzb_node *node, enum zzb_mac_status status);

/*
 * Returns true and reports the conflict if a frame for the address of the node carries another
 * destination IEEE address (R22 3.6.1.9.2)
 */
bool zzb_nwk_addr_conflict(struct zzb_node *node, const struct zzb_nwk_hdr *hdr);

/* Handles a received network status command */
void zzb_nwk_status_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
		       const struct zzb_nwk_cmd *cmd);

#endif /* ZZB_SUBSYS_NWK_NWK_H_ */
