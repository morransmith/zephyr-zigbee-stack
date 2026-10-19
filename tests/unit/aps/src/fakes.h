/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_APS_FAKES_H_
#define ZZB_TESTS_UNIT_APS_FAKES_H_

#include <stdint.h>
#include <zephyr/fff.h>

#include "aps/aps.h"
#include "nwk/nwk.h"
#include "sec/mmo.h"

DECLARE_FAKE_VALUE_FUNC(bool, zzb_nwk_key_pending, const struct zzb_node *);

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
