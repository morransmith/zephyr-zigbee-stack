/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_MAC_FRAME_H_
#define ZZB_SUBSYS_MAC_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include "common/buf.h"

/* Frame Control, Sequence Number, two PAN IDs and two extended addresses */
#define ZZB_MAC_HDR_MAX_LEN 23

enum zzb_mac_frame_type {
	ZZB_MAC_FRAME_BEACON = 0,
	ZZB_MAC_FRAME_DATA = 1,
	ZZB_MAC_FRAME_ACK = 2,
	ZZB_MAC_FRAME_CMD = 3,
};

struct zzb_mac_hdr {
	enum zzb_mac_frame_type type;
	/* ZZB_FRAME_ACK_REQ and ZZB_FRAME_PENDING */
	uint8_t flags;
	uint8_t seq;
	struct zzb_mac_addr dst;
	struct zzb_mac_addr src;
};

/* Returns the header length. The source PAN ID is omitted when it equals the destination one. */
size_t zzb_mac_hdr_len(const struct zzb_mac_hdr *hdr);

/* Writes hdr with frame version 0 (R22 D.5) and returns its length. */
size_t zzb_mac_hdr_write(const struct zzb_mac_hdr *hdr, uint8_t *p);

/*
 * Returns the header length, -EINVAL if the header is longer than len or has a reserved frame
 * type or addressing mode, or -ENOTSUP for frame version 2 and MAC security.
 */
int zzb_mac_hdr_parse(const uint8_t *p, size_t len, struct zzb_mac_hdr *hdr);

#endif /* ZZB_SUBSYS_MAC_FRAME_H_ */
