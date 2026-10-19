/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_APS_FRAME_H_
#define ZZB_SUBSYS_APS_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#include "sec/mmo.h"

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

enum zzb_aps_cmd_id {
	ZZB_APS_CMD_TRANSPORT_KEY = 0x05,
	ZZB_APS_CMD_REQUEST_KEY = 0x08,
	ZZB_APS_CMD_SWITCH_KEY = 0x09,
	ZZB_APS_CMD_VERIFY_KEY = 0x0f,
	ZZB_APS_CMD_CONFIRM_KEY = 0x10,
};

/* StandardKeyType (R22 Table 4-9) */
enum zzb_aps_key_type {
	ZZB_APS_KEY_NWK = 0x01,
	ZZB_APS_KEY_APP_LINK = 0x03,
	ZZB_APS_KEY_TC_LINK = 0x04,
};

/* RequestKeyType (R22 Table 4-19) */
enum zzb_aps_request_key_type {
	ZZB_APS_REQUEST_APP_LINK = 0x02,
	ZZB_APS_REQUEST_TC_LINK = 0x04,
};

struct zzb_aps_cmd {
	enum zzb_aps_cmd_id id;
	union {
		/* The key descriptor depends on the key type (R22 4.4.10.1.3) */
		struct {
			enum zzb_aps_key_type type;
			uint8_t key[ZZB_SEC_KEY_LEN];
			/* Network key */
			uint8_t seq;
			/* Network key and trust center link key */
			uint64_t dst;
			uint64_t src;
			/* Application link key */
			uint64_t partner;
			bool initiator;
		} transport_key;
		struct {
			enum zzb_aps_request_key_type type;
			/* Application link key */
			uint64_t partner;
		} request_key;
		/* ZZB_APS_CMD_SWITCH_KEY */
		uint8_t switch_key_seq;
		struct {
			enum zzb_aps_key_type type;
			uint64_t src;
			uint8_t hash[ZZB_SEC_KEY_LEN];
		} verify_key;
		struct {
			uint8_t status;
			enum zzb_aps_key_type type;
			uint64_t dst;
		} confirm_key;
	};
};

/* Writes the command identifier and the command payload and returns their length. */
size_t zzb_aps_cmd_write(const struct zzb_aps_cmd *cmd, uint8_t *p);

/*
 * Parses a command that starts at the command identifier. Returns the length of the fields read,
 * -EINVAL if the command is longer than len, or -ENOTSUP for another command identifier or a
 * reserved key type. Bytes after the command are ignored.
 */
int zzb_aps_cmd_parse(const uint8_t *p, size_t len, struct zzb_aps_cmd *cmd);

#endif /* ZZB_SUBSYS_APS_FRAME_H_ */
