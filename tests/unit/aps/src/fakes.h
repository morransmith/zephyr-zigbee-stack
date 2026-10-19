/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_APS_FAKES_H_
#define ZZB_TESTS_UNIT_APS_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "nwk/nwk.h"
#include "sec/mmo.h"

DECLARE_FAKE_VALUE_FUNC(bool, zzb_nwk_key_pending, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlde_data_req, struct zzb_node *, struct net_buf *, uint16_t,
			uint8_t, bool);
DECLARE_FAKE_VALUE_FUNC(struct zzb_nwk_neighbor *, zzb_nwk_neighbor_find_short, struct zzb_node *,
			uint16_t);
DECLARE_FAKE_VALUE_FUNC(struct zzb_nwk_neighbor *, zzb_nwk_neighbor_find_ext, struct zzb_node *,
			uint64_t);
DECLARE_FAKE_VOID_FUNC(zzb_apsde_data_conf, struct zzb_node *, uint8_t, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_apsde_data_ind, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_ind *);

/* Copy of a frame passed to NLDE-DATA.request or APSDE-DATA.indication */
struct frame_copy {
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	struct zzb_frame_meta meta;
};

/* By default the zzb_nlde_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[8];

/* The ASDU and the parameters of the last APSDE-DATA.indication */
extern struct frame_copy data_ind;
extern struct zzb_apsde_ind ind;

/* Checks that every frame buffer is free */
void check_no_buffer_leak(void);

/* Destination and source of the zigbee-rs Transport Key in tests/unit/sec (see NOTICE) */
#define NODE_EUI64 0xa4c1389c383001e5ULL
#define TC_EUI64   0xf4ce36c17d3852e1ULL

/* Trust center link key B of BDB 10.2.5 */
extern const uint8_t key_b[ZZB_SEC_KEY_LEN];

/* Node 0 has NODE_EUI64, node 1 plays the trust center with TC_EUI64 */
struct zzb_node *test_node(int index);

/* Resets every fake */
void reset_fakes(void);

/* Runs the stack thread until it waits, without advancing the time */
void run_stack(void);

#endif /* ZZB_TESTS_UNIT_APS_FAKES_H_ */
