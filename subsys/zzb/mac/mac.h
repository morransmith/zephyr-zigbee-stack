/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_MAC_MAC_H_
#define ZZB_SUBSYS_MAC_MAC_H_

#include <stdint.h>
#include <zephyr/device.h>

struct zzb_node;

/* Numeric values from 802.15.4-2006 */
enum zzb_mac_status {
	ZZB_MAC_STATUS_SUCCESS = 0x00,
	ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE = 0xe1,
	ZZB_MAC_STATUS_NO_ACK = 0xe9,
};

/* MAC and PHY PIB attributes of the node */
struct zzb_mac {
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t channel;
};

/* Binds the node to radio, programs the address filters and starts the radio */
int zzb_mac_init(struct zzb_node *node, const struct device *radio);

#endif /* ZZB_SUBSYS_MAC_MAC_H_ */
