/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_NWK_FRAME_H_
#define ZZB_SUBSYS_NWK_FRAME_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

/* nwkcProtocolVersion */
#define ZZB_NWK_PROTOCOL_VERSION 2

/* nwkMaxSourceRoute */
#define ZZB_NWK_MAX_RELAYS 12

enum zzb_nwk_frame_type {
	ZZB_NWK_FRAME_DATA = 0,
	ZZB_NWK_FRAME_CMD = 1,
};

/* zzb_nwk_hdr.flags, at the positions of the frame control field */
#define ZZB_NWK_DISCOVER_ROUTE BIT(6)
#define ZZB_NWK_MULTICAST      BIT(8)
#define ZZB_NWK_SECURITY       BIT(9)
#define ZZB_NWK_SRC_ROUTE      BIT(10)
#define ZZB_NWK_DST_IEEE       BIT(11)
#define ZZB_NWK_SRC_IEEE       BIT(12)
#define ZZB_NWK_ED_INITIATOR   BIT(13)

struct zzb_nwk_hdr {
	enum zzb_nwk_frame_type type;
	uint16_t flags;
	uint16_t dst;
	uint16_t src;
	uint8_t radius;
	uint8_t seq;
	/* Present with ZZB_NWK_DST_IEEE and ZZB_NWK_SRC_IEEE */
	uint64_t dst_ext;
	uint64_t src_ext;
	/* Present with ZZB_NWK_MULTICAST */
	uint8_t mcast_ctrl;
	/* Source route subframe, present with ZZB_NWK_SRC_ROUTE */
	uint8_t relay_count;
	uint8_t relay_index;
	uint16_t relays[ZZB_NWK_MAX_RELAYS];
};

/* Returns the header length. */
size_t zzb_nwk_hdr_len(const struct zzb_nwk_hdr *hdr);

/* Writes hdr with protocol version 2 and returns its length. */
size_t zzb_nwk_hdr_write(const struct zzb_nwk_hdr *hdr, uint8_t *p);

/*
 * Returns the header length, -EINVAL if the header is longer than len or has the reserved frame
 * type, or -ENOTSUP for another protocol version, an inter-PAN frame or more than
 * ZZB_NWK_MAX_RELAYS relays.
 */
int zzb_nwk_hdr_parse(const uint8_t *p, size_t len, struct zzb_nwk_hdr *hdr);

#endif /* ZZB_SUBSYS_NWK_FRAME_H_ */
