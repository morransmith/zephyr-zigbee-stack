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

/* Superframe Specification field */
#define ZZB_MAC_SF_PAN_COORD    BIT(14)
#define ZZB_MAC_SF_ASSOC_PERMIT BIT(15)

/*
 * Writes the beacon MAC payload up to the beacon payload: the Superframe Specification field and
 * empty GTS and pending address fields. Returns its length, 4.
 */
size_t zzb_mac_beacon_write(uint16_t superframe, uint8_t *p);

/*
 * Parses the beacon MAC payload. Returns the offset of the beacon payload, or -EINVAL if the
 * fields before it are longer than len.
 */
int zzb_mac_beacon_parse(const uint8_t *p, size_t len, uint16_t *superframe);

/* Command Frame Identifiers of the commands Zigbee uses */
enum zzb_mac_cmd_id {
	ZZB_MAC_CMD_ASSOC_REQ = 0x01,
	ZZB_MAC_CMD_ASSOC_RSP = 0x02,
	ZZB_MAC_CMD_DATA_REQ = 0x04,
	ZZB_MAC_CMD_ORPHAN = 0x06,
	ZZB_MAC_CMD_BEACON_REQ = 0x07,
	ZZB_MAC_CMD_COORD_REALIGN = 0x08,
};

struct zzb_mac_cmd {
	enum zzb_mac_cmd_id id;
	union {
		/* ZZB_MAC_CMD_ASSOC_REQ */
		uint8_t capability;
		/* ZZB_MAC_CMD_ASSOC_RSP */
		struct {
			uint16_t short_addr;
			uint8_t status;
		} assoc_rsp;
		/* ZZB_MAC_CMD_COORD_REALIGN */
		struct {
			uint16_t pan_id;
			uint16_t coord_short_addr;
			uint8_t channel;
			uint16_t short_addr;
		} realign;
	};
};

/*
 * Writes the Command Frame Identifier and the command fields and returns their length.
 * Coordinator realignment is written without the Channel Page field (R22 D.5).
 */
size_t zzb_mac_cmd_write(const struct zzb_mac_cmd *cmd, uint8_t *p);

/*
 * Parses a command that starts at the Command Frame Identifier. Returns the length of the fields
 * read, -EINVAL if the command is longer than len, or -ENOTSUP for a command Zigbee does not use.
 * A Channel Page field after a coordinator realignment command is ignored.
 */
int zzb_mac_cmd_parse(const uint8_t *p, size_t len, struct zzb_mac_cmd *cmd);

#endif /* ZZB_SUBSYS_MAC_FRAME_H_ */
