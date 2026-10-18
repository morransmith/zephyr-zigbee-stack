/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_BUF_H_
#define ZZB_SUBSYS_COMMON_BUF_H_

#include <stdint.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

/* aMaxPHYPacketSize */
#define ZZB_BUF_SIZE 127

/* Values of the addressing mode fields of the MAC frame control field */
enum zzb_mac_addr_mode {
	ZZB_MAC_ADDR_NONE = 0,
	ZZB_MAC_ADDR_SHORT = 2,
	ZZB_MAC_ADDR_EXT = 3,
};

struct zzb_mac_addr {
	uint64_t ext_addr;
	uint16_t short_addr;
	uint16_t pan_id;
	enum zzb_mac_addr_mode mode;
};

/* zzb_frame_meta.flags and zzb_mac_hdr.flags */
#define ZZB_FRAME_ACK_REQ  BIT(0)
#define ZZB_FRAME_INDIRECT BIT(1)
#define ZZB_FRAME_PENDING  BIT(2)

struct zzb_frame_meta {
	struct zzb_mac_addr src;
	struct zzb_mac_addr dst;
	uint8_t handle;
	uint8_t lqi;
	int8_t rssi;
	uint8_t flags;
};

/* Returns an empty buffer with zeroed metadata, or NULL on timeout. */
struct net_buf *zzb_buf_alloc(k_timeout_t timeout);

static inline struct zzb_frame_meta *zzb_frame_meta(struct net_buf *buf)
{
	return net_buf_user_data(buf);
}

#endif /* ZZB_SUBSYS_COMMON_BUF_H_ */
