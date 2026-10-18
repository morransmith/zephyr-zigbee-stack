/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_NWK_FRAME_H_
#define ZZB_SUBSYS_NWK_FRAME_H_

#include <stdbool.h>
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

enum zzb_nwk_cmd_id {
	ZZB_NWK_CMD_ROUTE_REQ = 0x01,
	ZZB_NWK_CMD_ROUTE_REPLY = 0x02,
	ZZB_NWK_CMD_NWK_STATUS = 0x03,
	ZZB_NWK_CMD_LEAVE = 0x04,
	ZZB_NWK_CMD_ROUTE_RECORD = 0x05,
	ZZB_NWK_CMD_REJOIN_REQ = 0x06,
	ZZB_NWK_CMD_REJOIN_RSP = 0x07,
	ZZB_NWK_CMD_LINK_STATUS = 0x08,
	ZZB_NWK_CMD_NWK_REPORT = 0x09,
	ZZB_NWK_CMD_NWK_UPDATE = 0x0a,
	ZZB_NWK_CMD_ED_TIMEOUT_REQ = 0x0b,
	ZZB_NWK_CMD_ED_TIMEOUT_RSP = 0x0c,
	ZZB_NWK_CMD_LINK_POWER_DELTA = 0x0d,
};

/* Command options fields */
#define ZZB_NWK_ROUTE_REQ_MANY_TO_ONE   GENMASK(4, 3)
#define ZZB_NWK_ROUTE_REQ_DST_IEEE      BIT(5)
#define ZZB_NWK_ROUTE_REQ_MULTICAST     BIT(6)
#define ZZB_NWK_ROUTE_REPLY_ORIG_IEEE   BIT(4)
#define ZZB_NWK_ROUTE_REPLY_RESP_IEEE   BIT(5)
#define ZZB_NWK_ROUTE_REPLY_MULTICAST   BIT(6)
#define ZZB_NWK_LEAVE_REJOIN            BIT(5)
#define ZZB_NWK_LEAVE_REQUEST           BIT(6)
#define ZZB_NWK_LEAVE_REMOVE_CHILDREN   BIT(7)
#define ZZB_NWK_LINK_STATUS_FIRST_FRAME BIT(5)
#define ZZB_NWK_LINK_STATUS_LAST_FRAME  BIT(6)

/*
 * Link status entry: neighbor network address, then incoming cost in bits 0-2 and outgoing cost
 * in bits 4-6. Link power delta entry: device network address, then power delta in dB.
 */
#define ZZB_NWK_LINK_STATUS_ENTRY_LEN 3
#define ZZB_NWK_POWER_DELTA_ENTRY_LEN 3

/*
 * Lists point to their first encoded entry: into the frame after parsing, to the entries to copy
 * for writing. Route record relays and PAN IDs are 2-byte entries.
 */
struct zzb_nwk_cmd {
	enum zzb_nwk_cmd_id id;
	union {
		struct {
			uint8_t options;
			uint8_t id;
			uint16_t dst;
			uint8_t path_cost;
			/* Present with ZZB_NWK_ROUTE_REQ_DST_IEEE */
			uint64_t dst_ext;
		} route_req;
		struct {
			uint8_t options;
			uint8_t id;
			uint16_t orig;
			uint16_t resp;
			uint8_t path_cost;
			/* Present with the ORIG_IEEE and RESP_IEEE options */
			uint64_t orig_ext;
			uint64_t resp_ext;
		} route_reply;
		struct {
			uint8_t code;
			/* Present after a routing failure or an address conflict (R22 3.4.3.3.2) */
			bool has_addr;
			uint16_t addr;
		} nwk_status;
		/* ZZB_NWK_CMD_LEAVE */
		uint8_t leave_options;
		struct {
			uint8_t count;
			const uint8_t *relays;
		} route_record;
		/* ZZB_NWK_CMD_REJOIN_REQ */
		uint8_t capability;
		struct {
			uint16_t short_addr;
			uint8_t status;
		} rejoin_rsp;
		struct {
			/* ZZB_NWK_LINK_STATUS_FIRST_FRAME and ZZB_NWK_LINK_STATUS_LAST_FRAME */
			uint8_t flags;
			uint8_t count;
			const uint8_t *entries;
		} link_status;
		/* PAN identifier conflict report */
		struct {
			uint64_t epid;
			uint8_t count;
			const uint8_t *pan_ids;
		} nwk_report;
		/* PAN identifier update */
		struct {
			uint64_t epid;
			uint8_t update_id;
			uint16_t pan_id;
		} nwk_update;
		struct {
			uint8_t timeout;
			uint8_t config;
		} ed_timeout_req;
		struct {
			uint8_t status;
			uint8_t parent_info;
		} ed_timeout_rsp;
		struct {
			uint8_t type;
			uint8_t count;
			const uint8_t *entries;
		} power_delta;
	};
};

/* Writes the command identifier and the command payload and returns their length. */
size_t zzb_nwk_cmd_write(const struct zzb_nwk_cmd *cmd, uint8_t *p);

/*
 * Parses a command that starts at the command identifier. Returns the length of the fields read,
 * -EINVAL if the command is longer than len or a PAN identifier update does not hold one PAN ID,
 * or -ENOTSUP for a reserved command identifier or report or update type. Bytes after the
 * command are ignored.
 */
int zzb_nwk_cmd_parse(const uint8_t *p, size_t len, struct zzb_nwk_cmd *cmd);

#endif /* ZZB_SUBSYS_NWK_FRAME_H_ */
