/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_MAC_MAC_H_
#define ZZB_SUBSYS_MAC_MAC_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "mac/frame.h"

struct zzb_node;

/* Numeric values from 802.15.4-2006 */
enum zzb_mac_status {
	ZZB_MAC_STATUS_SUCCESS = 0x00,
	/* Association status values of the association response command */
	ZZB_MAC_STATUS_PAN_AT_CAPACITY = 0x01,
	ZZB_MAC_STATUS_PAN_ACCESS_DENIED = 0x02,
	ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE = 0xe1,
	ZZB_MAC_STATUS_NO_ACK = 0xe9,
	ZZB_MAC_STATUS_NO_BEACON = 0xea,
	ZZB_MAC_STATUS_NO_DATA = 0xeb,
};

enum zzb_mac_op {
	ZZB_MAC_OP_NONE,
	ZZB_MAC_OP_SCAN,
	/* Data request in flight */
	ZZB_MAC_OP_POLL,
	/* Waiting for the frame announced by the ACK of the data request */
	ZZB_MAC_OP_POLL_RX,
	/* Association request in flight, then macResponseWaitTime */
	ZZB_MAC_OP_ASSOC,
	/* Data request for the association response in flight */
	ZZB_MAC_OP_ASSOC_POLL,
	/* Waiting for the association response */
	ZZB_MAC_OP_ASSOC_RX,
};

struct zzb_mac {
	/* MAC and PHY PIB attributes */
	uint64_t coord_ext_addr;
	uint16_t coord_short_addr;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t dsn;
	uint8_t channel;
	/* Data and command frames; the head is in flight */
	struct k_fifo tx_fifo;
	uint8_t tx_retries;
	/* The MLME procedure in progress, its command frame and its timer */
	enum zzb_mac_op op;
	struct net_buf *cmd_buf;
	struct k_work_delayable timer;
	/* Active scan: channels left, the channel scanned, ScanDuration, whether a beacon came */
	uint32_t scan_channels;
	uint8_t scan_channel;
	uint8_t scan_duration;
	bool scan_beacon;
	/* Coordinator of the poll or association in progress */
	struct zzb_mac_addr coord;
};

/*
 * Binds the node to radio, programs the address filters and starts the radio, which a sleepy node
 * stops again. Returns -ENOTSUP for a sleepy node whose radio does not report
 * IEEE802154_RX_ON_WHEN_IDLE.
 */
int zzb_mac_init(struct zzb_node *node, const struct device *radio);

/*
 * MCPS-DATA.request. buf holds the MSDU after ZZB_MAC_HDR_MAX_LEN bytes of headroom, and its
 * metadata the destination, ZZB_FRAME_ACK_REQ and the handle. Returns -EMSGSIZE if the headroom
 * is shorter than the MAC header or the frame is longer than 125 bytes.
 */
int zzb_mcps_data_req(struct zzb_node *node, struct net_buf *buf);

/*
 * MLME-SCAN.request for an active scan. Bit n of channels selects channel n (11 to 26). Each
 * channel is scanned for aBaseSuperframeDuration * (2^duration + 1) symbols.
 */
int zzb_mlme_scan_req(struct zzb_node *node, uint32_t channels, uint8_t duration);

/* MLME-POLL.request: extracts one frame from coord */
int zzb_mlme_poll_req(struct zzb_node *node, const struct zzb_mac_addr *coord);

/* MLME-ASSOCIATE.request to coord on channel */
int zzb_mlme_assoc_req(struct zzb_node *node, uint8_t channel, const struct zzb_mac_addr *coord,
		       uint8_t capability);

/* MLME-SET of macPANId and macShortAddress, which also programs the radio filter */
void zzb_mlme_set_pan_id(struct zzb_node *node, uint16_t pan_id);
void zzb_mlme_set_short_addr(struct zzb_node *node, uint16_t short_addr);

/*
 * MLME-SET of phyCurrentChannel. Returns -EBUSY during an MLME procedure, whose end may restore
 * the channel, or the error of the radio.
 */
int zzb_mlme_set_channel(struct zzb_node *node, uint8_t channel);

/* MLME-GET of macCoordExtendedAddress, set by a successful association */
uint64_t zzb_mlme_get_coord_ext_addr(const struct zzb_node *node);

/* Defined by NWK */
void zzb_mcps_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status);

/* Defined by NWK. buf holds the MSDU, and its metadata the addresses, flags, LQI and RSSI. */
void zzb_mcps_data_ind(struct zzb_node *node, struct net_buf *buf);

/* Defined by NWK. buf holds the beacon payload, and its metadata the coordinator address. */
void zzb_mlme_beacon_notify_ind(struct zzb_node *node, struct net_buf *buf, uint8_t channel,
				uint16_t superframe);

/* Defined by NWK. SUCCESS if a beacon was received, otherwise NO_BEACON. */
void zzb_mlme_scan_conf(struct zzb_node *node, enum zzb_mac_status status);

/*
 * Defined by NWK. SUCCESS if a frame was received; its MCPS-DATA.indication follows. NO_DATA if
 * the coordinator had no frame.
 */
void zzb_mlme_poll_conf(struct zzb_node *node, enum zzb_mac_status status);

/* Defined by NWK. short_addr is 0xffff unless status is SUCCESS. */
void zzb_mlme_assoc_conf(struct zzb_node *node, uint16_t short_addr, enum zzb_mac_status status);

/* aBaseSuperframeDuration: 960 symbols of 16 us */
#define ZZB_MAC_BASE_SUPERFRAME_US 15360

/* macResponseWaitTime: 32 * aBaseSuperframeDuration */
#define ZZB_MAC_RESPONSE_WAIT_US (32 * ZZB_MAC_BASE_SUPERFRAME_US)

/* Within the MAC */

/*
 * macMaxFrameTotalWaitTime: 9846 symbols, from the formula of 802.15.4 Table 52 with macMinBE 5,
 * macMaxBE 8 and macMaxCSMABackoffs 4 of the parent (R22 D.6)
 */
#define ZZB_MAC_MAX_FRAME_TOTAL_WAIT_US 157536

/*
 * Allocates the command buffer for op, or returns -EBUSY while another procedure runs, or for a
 * scan or an association while the TX FIFO holds frames
 */
int zzb_mac_start_op(struct zzb_node *node, enum zzb_mac_op op);

/* Releases the command buffer and ends the procedure */
void zzb_mac_end_op(struct zzb_node *node);

/* Sends cmd from the command buffer; its confirm reaches zzb_mac_cmd_done() */
void zzb_mac_send_cmd(struct zzb_node *node, struct zzb_mac_hdr *hdr,
		      const struct zzb_mac_cmd *cmd);
void zzb_mac_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending);

void zzb_mac_set_radio_pan_id(struct zzb_node *node, uint16_t pan_id);
void zzb_mac_set_radio_short_addr(struct zzb_node *node, uint16_t short_addr);

/*
 * Sets the channel and restarts RX, since esp32 applies a new channel only then. Outside an MLME
 * procedure a sleepy node then sets RxOnWhenIdle FALSE and stops the radio.
 */
int zzb_mac_set_radio_channel(struct zzb_node *node, uint8_t channel);

/* Short address once one is assigned, otherwise the extended address (802.15.4 5.1.6.1) */
struct zzb_mac_addr zzb_mac_src_addr(const struct zzb_node *node);

/* Scans the next channel, or ends the scan after the last one */
void zzb_mac_scan_next(struct zzb_node *node);
void zzb_mac_scan_cmd_done(struct zzb_node *node, enum zzb_mac_status status);
void zzb_mac_scan_rx(struct zzb_node *node, struct net_buf *buf);

void zzb_mac_poll_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending);
void zzb_mac_poll_timeout(struct zzb_node *node);
/* Takes a data frame from the polled coordinator */
void zzb_mac_poll_rx(struct zzb_node *node, struct net_buf *buf);

void zzb_mac_assoc_cmd_done(struct zzb_node *node, enum zzb_mac_status status, bool frame_pending);
void zzb_mac_assoc_timeout(struct zzb_node *node);
/* Takes a command frame received while waiting for the association response */
void zzb_mac_assoc_rx(struct zzb_node *node, struct net_buf *buf);

#endif /* ZZB_SUBSYS_MAC_MAC_H_ */
