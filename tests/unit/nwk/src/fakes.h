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

/* Copy of a frame passed to MCPS-DATA.request or NLDE-DATA.indication */
struct frame_copy {
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	struct zzb_frame_meta meta;
};

/* By default the zzb_mcps_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[4];
extern struct frame_copy data_ind;

/* Coordinator of the last MLME-ASSOCIATE.request */
extern struct zzb_mac_addr assoc_coord;

/* Checks that every frame buffer is free */
void check_no_buffer_leak(void);

#define NODE_EUI64   0x0011223344556677ULL
/* Source of the Ziggurat frame in tests/unit/sec (see NOTICE) */
#define SENDER_EUI64 0x00124b001e17efa8ULL

/* Node 0 has NODE_EUI64, node 1 SENDER_EUI64; both are initialized on the first call */
struct zzb_node *test_node(int index);

/* Resets every fake */
void reset_fakes(void);

/* Passes a beacon from short_addr in PAN pan_id as MLME-BEACON-NOTIFY.indication */
void notify_beacon(struct zzb_node *node, const struct zzb_nwk_beacon *beacon, uint16_t pan_id,
		   uint16_t short_addr, uint8_t channel, uint16_t superframe, uint8_t lqi);

#endif /* ZZB_TESTS_UNIT_NWK_FAKES_H_ */
