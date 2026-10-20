/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_NWK_FAKES_H_
#define ZZB_TESTS_UNIT_NWK_FAKES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>

#include "common/buf.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_mcps_data_req, struct zzb_node *, struct net_buf *);
DECLARE_FAKE_VOID_FUNC(zzb_nlde_data_conf, struct zzb_node *, uint8_t, enum zzb_mac_status);
DECLARE_FAKE_VOID_FUNC(zzb_nlde_data_ind, struct zzb_node *, struct net_buf *, uint16_t, uint16_t,
		       bool);
DECLARE_FAKE_VALUE_FUNC(int, zzb_mlme_scan_req, struct zzb_node *, uint32_t, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_nlme_network_discovery_conf, struct zzb_node *, uint8_t,
		       const struct zzb_nwk_network *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_mlme_assoc_req, struct zzb_node *, uint8_t,
			const struct zzb_mac_addr *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_mlme_get_coord_ext_addr, const struct zzb_node *);
DECLARE_FAKE_VOID_FUNC(zzb_nlme_join_conf, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_nlme_nwk_status_ind, struct zzb_node *, uint8_t, uint16_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_mlme_poll_req, struct zzb_node *, const struct zzb_mac_addr *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_mlme_set_channel, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_nlme_leave_conf, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_nlme_leave_ind, struct zzb_node *, uint64_t, bool);

/* Copy of a frame passed to MCPS-DATA.request or NLDE-DATA.indication */
struct frame_copy {
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	struct zzb_frame_meta meta;
};

/* By default the zzb_mcps_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[8];
extern struct frame_copy data_ind;

/* Coordinator of the last MLME-ASSOCIATE.request and MLME-POLL.request */
extern struct zzb_mac_addr assoc_coord;
extern struct zzb_mac_addr poll_coord;

/* Checks that every frame buffer is free */
void check_no_buffer_leak(void);

/* The test network of pair_nodes() */
#define PAN_ID       0x1a62
#define EXT_PAN_ID   0x00124b0000001a62ULL
#define CHILD_SHORT  0x1234
#define PARENT_SHORT 0x0000

extern const uint8_t test_key[ZZB_SEC_KEY_LEN];

#define NODE_EUI64   0x0011223344556677ULL
/* Source of the Ziggurat frame in tests/unit/sec (see NOTICE) */
#define SENDER_EUI64 0x00124b001e17efa8ULL

/* Node 0 has NODE_EUI64, node 1 SENDER_EUI64; both are initialized on the first call */
struct zzb_node *test_node(int index);

/* Resets every fake */
void reset_fakes(void);

/* Offsets in the record nwk/net */
#define NET_SHORT_ADDR   11
#define NET_PARENT_INFO  16
#define NET_PARENT_SHORT 17

/* Reads the record zzb/0/<key>; returns its length, 0 when there is none */
int load_record(const char *key, uint8_t *rec, size_t len);

/* Deletes the NWK records of node 0 */
void delete_nwk_records(void);

/* Resets the fakes, then does what zzb_node_init() does for NWK on boot for node 0 */
int nwk_reboot(void);

/* Runs the stack thread until it waits, without advancing the time */
void run_stack(void);

/* Unsecures a copy of sent frame i as receiver and parses its header and command */
void parse_sent_cmd(int i, struct zzb_node *receiver, struct zzb_nwk_hdr *hdr,
		    struct zzb_nwk_cmd *cmd);

/* Initializes both nodes and makes child the child of parent on the test network */
void pair_nodes(struct zzb_node *child, struct zzb_node *parent);

/* MAC flags of the frames that receive() and receive_frame() pass; reset_fakes() clears them */
extern uint8_t receive_flags;

/* Passes frame to node as MCPS-DATA.indication from the MAC source address src */
void receive(struct zzb_node *node, const uint8_t *frame, size_t len, uint16_t src);

/*
 * Passes a frame with hdr and payload from sender to node, secured by sender when hdr has
 * ZZB_NWK_SECURITY
 */
void receive_frame(struct zzb_node *node, struct zzb_node *sender, const struct zzb_nwk_hdr *hdr,
		   const uint8_t *payload, size_t len);
void receive_cmd(struct zzb_node *node, struct zzb_node *sender, const struct zzb_nwk_hdr *hdr,
		 const struct zzb_nwk_cmd *cmd);

/* Passes a beacon from short_addr in PAN pan_id as MLME-BEACON-NOTIFY.indication */
void notify_beacon(struct zzb_node *node, const struct zzb_nwk_beacon *beacon, uint16_t pan_id,
		   uint16_t short_addr, uint8_t channel, uint16_t superframe, uint8_t lqi);

#endif /* ZZB_TESTS_UNIT_NWK_FAKES_H_ */
