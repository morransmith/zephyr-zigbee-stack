/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZDO_FAKES_H_
#define ZZB_TESTS_UNIT_ZDO_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "common/buf.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);

#define NODE_EUI64 0x00124b0001020304ULL
#define NODE_ADDR  0x5678

/* The coordinator sends the requests */
#define REQUESTER_ADDR 0x0000

/* Copy of a frame passed to APSDE-DATA.request */
struct frame_copy {
	struct zzb_apsde_req req;
	uint8_t handle;
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
};

/* By default the zzb_apsde_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[8];

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
