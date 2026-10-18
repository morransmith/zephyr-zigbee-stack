/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_MAC_MAC_H_
#define ZZB_SUBSYS_MAC_MAC_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

struct zzb_node;

/* Numeric values from 802.15.4-2006 */
enum zzb_mac_status {
	ZZB_MAC_STATUS_SUCCESS = 0x00,
	ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE = 0xe1,
	ZZB_MAC_STATUS_NO_ACK = 0xe9,
};

struct zzb_mac {
	/* MAC and PHY PIB attributes */
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t dsn;
	uint8_t channel;
	/* Data and command frames; the head is in flight */
	struct k_fifo tx_fifo;
	uint8_t tx_retries;
};

/* Binds the node to radio, programs the address filters and starts the radio */
int zzb_mac_init(struct zzb_node *node, const struct device *radio);

/*
 * MCPS-DATA.request. buf holds the MSDU after ZZB_MAC_HDR_MAX_LEN bytes of headroom, and its
 * metadata the destination, ZZB_FRAME_ACK_REQ and the handle. Returns -EMSGSIZE if the headroom
 * is shorter than the MAC header or the frame is longer than 125 bytes.
 */
int zzb_mcps_data_req(struct zzb_node *node, struct net_buf *buf);

/* Defined by NWK */
void zzb_mcps_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status);

/* Defined by NWK. buf holds the MSDU, and its metadata the addresses, flags, LQI and RSSI. */
void zzb_mcps_data_ind(struct zzb_node *node, struct net_buf *buf);

#endif /* ZZB_SUBSYS_MAC_MAC_H_ */
