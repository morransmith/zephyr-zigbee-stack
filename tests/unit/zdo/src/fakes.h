/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZDO_FAKES_H_
#define ZZB_TESTS_UNIT_ZDO_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "nwk/nwk.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_set_tc_addr, struct zzb_node *, uint64_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_set_active_key, struct zzb_node *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(uint16_t, zzb_nlme_get_short_addr, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint8_t, zzb_nlme_get_capability, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_nlme_get_ext_pan_id, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_set_channel, struct zzb_node *, uint8_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nwk_addr_map_find, struct zzb_node *, uint64_t, uint16_t *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_set_addr_map, struct zzb_node *, uint64_t, uint16_t);
DECLARE_FAKE_VOID_FUNC(zzb_nwk_poll_hold, struct zzb_node *, enum zzb_nwk_hold, bool);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_nwk_key_ind, struct zzb_node *);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_leave, struct zzb_node *, bool);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_tc_link_key_ind, struct zzb_node *, uint64_t, const uint8_t *);
DECLARE_FAKE_VOID_FUNC(zzb_zdo_node_desc_conf, struct zzb_node *, uint8_t, uint16_t, uint16_t);
DECLARE_FAKE_VOID_FUNC(zzb_zdo_simple_desc_conf, struct zzb_node *, uint8_t, uint16_t,
		       const struct zzb_zdo_simple_desc *);
DECLARE_FAKE_VOID_FUNC(zzb_zdo_ieee_addr_conf, struct zzb_node *, uint8_t, uint16_t, uint64_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_bind_req, struct zzb_node *, const struct zzb_aps_binding *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_unbind_req, struct zzb_node *,
			const struct zzb_aps_binding *);
DECLARE_FAKE_VOID_FUNC(zzb_zcl_data_ind, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_ind *);

#define NODE_EUI64 0x00124b0001020304ULL
#define NODE_ADDR  0x5678

/* The coordinator sends the requests */
#define REQUESTER_ADDR 0x0000

/* The descriptor of the last zzb_zdo_simple_desc_conf() with SUCCESS, and its cluster lists */
extern struct zzb_zdo_simple_desc simple_desc;
extern uint8_t simple_desc_in[16];
extern uint8_t simple_desc_out[16];

/* Copy of a frame passed to APSDE-DATA.request */
struct frame_copy {
	struct zzb_apsde_req req;
	uint8_t handle;
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
};

/* By default the zzb_apsde_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[8];

/*
 * Calls of the zzb_nlme_set_nwk_key() fake. FFF declares the array parameter as a pointer, which
 * -Werror=array-parameter rejects.
 */
struct set_nwk_key_calls {
	int count;
	const uint8_t *key;
	uint8_t seq;
};

extern struct set_nwk_key_calls set_nwk_key;

/* The binding of the last APSME-BIND or APSME-UNBIND */
extern struct zzb_aps_binding binding;

struct zzb_node *test_node(void);

/* Resets every fake */
void reset_fakes(void);

/* Checks that every frame buffer is free */
void check_no_buffer_leak(void);

/* Passes the ASDU to ZDO as APSDE-DATA.indication with the parameters of ind */
void receive_ind(const struct zzb_apsde_ind *ind, const uint8_t *asdu, size_t len);

/* Passes a ZDO frame of cluster from REQUESTER_ADDR to dst_addr */
void receive(uint16_t cluster, uint16_t dst_addr, const uint8_t *asdu, size_t len);

/* Checks that frame i is the ZDO response of cluster to REQUESTER_ADDR with the ASDU */
void check_response(int i, uint16_t cluster, const uint8_t *asdu, size_t len);

#endif /* ZZB_TESTS_UNIT_ZDO_FAKES_H_ */
