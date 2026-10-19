/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_APS_FRAME_H_
#define ZZB_SUBSYS_APS_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

/* APS header with a group address or a destination endpoint, without extended header */
#define ZZB_APS_HDR_MAX_LEN 9

enum zzb_aps_frame_type {
	ZZB_APS_FRAME_DATA = 0,
	ZZB_APS_FRAME_CMD = 1,
	ZZB_APS_FRAME_ACK = 2,
};

enum zzb_aps_delivery {
	ZZB_APS_DELIVERY_UNICAST = 0,
	ZZB_APS_DELIVERY_BROADCAST = 2,
	ZZB_APS_DELIVERY_GROUP = 3,
};

/* zzb_aps_hdr.flags, at the positions of the frame control field */
#define ZZB_APS_ACK_FORMAT BIT(4)
#define ZZB_APS_SECURITY   BIT(5)
#define ZZB_APS_ACK_REQ    BIT(6)
#define ZZB_APS_EXT_HDR    BIT(7)

/*
 * Data frames and ACKs without ZZB_APS_ACK_FORMAT hold the cluster, profile and source endpoint,
 * and the destination endpoint unless the delivery is to a group. A command and its ACK hold only
 * the counter. The extended header is a frame control octet without fragmentation.
 */
struct zzb_aps_hdr {
	enum zzb_aps_frame_type type;
	enum zzb_aps_delivery delivery;
	uint8_t flags;
	uint8_t dst_ep;
	uint16_t group;
	uint16_t cluster;
	uint16_t profile;
	uint8_t src_ep;
	uint8_t counter;
};

/* Returns the header length. */
size_t zzb_aps_hdr_len(const struct zzb_aps_hdr *hdr);

/* Writes hdr and returns its length. */
size_t zzb_aps_hdr_write(const struct zzb_aps_hdr *hdr, uint8_t *p);

/*
 * Returns the header length, -EINVAL if the header is longer than len or has the reserved
 * delivery mode or reserved extended frame control bits, or -ENOTSUP for an inter-PAN or
 * fragmented frame.
 */
int zzb_aps_hdr_parse(const uint8_t *p, size_t len, struct zzb_aps_hdr *hdr);

#endif /* ZZB_SUBSYS_APS_FRAME_H_ */
