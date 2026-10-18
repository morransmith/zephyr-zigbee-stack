/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_NWK_FAKES_H_
#define ZZB_TESTS_UNIT_NWK_FAKES_H_

#include <stdint.h>
#include <zephyr/fff.h>

#include "mac/mac.h"

DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);

#define NODE_EUI64   0x0011223344556677ULL
/* Source of the Ziggurat frame in tests/unit/sec (see NOTICE) */
#define SENDER_EUI64 0x00124b001e17efa8ULL

/* Node 0 has NODE_EUI64, node 1 SENDER_EUI64; both are initialized on the first call */
struct zzb_node *test_node(int index);

/* Resets every fake */
void reset_fakes(void);

#endif /* ZZB_TESTS_UNIT_NWK_FAKES_H_ */
