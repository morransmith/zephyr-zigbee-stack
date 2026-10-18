/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_MAC_MAC_H_
#define ZZB_SUBSYS_MAC_MAC_H_

/* Numeric values from 802.15.4-2006 */
enum zzb_mac_status {
	ZZB_MAC_STATUS_SUCCESS = 0x00,
	ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE = 0xe1,
	ZZB_MAC_STATUS_NO_ACK = 0xe9,
};

#endif /* ZZB_SUBSYS_MAC_MAC_H_ */
