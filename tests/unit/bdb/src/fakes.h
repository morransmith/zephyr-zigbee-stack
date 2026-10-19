/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_BDB_FAKES_H_
#define ZZB_TESTS_UNIT_BDB_FAKES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zzb/bdb.h>

#include "nwk/nwk.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_join_req, struct zzb_node *, uint64_t,
			enum zzb_nwk_join_method, uint32_t, uint8_t, uint8_t, bool);
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_nlme_get_ext_pan_id, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint8_t, zzb_nlme_get_capability, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint16_t, zzb_nlme_get_short_addr, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_device_annce_req, struct zzb_node *);

#define EXT_PAN_ID 0x00124b0000001a62ULL
#define NODE_ADDR  0x5678

/* nwkCapabilityInformation: allocate address, receiver on when idle, mains power */
#define CAPABILITY 0x8c

/* The events of the BDB callback */
struct event_log {
	int count;
	enum zzb_bdb_event event;
	enum zzb_bdb_status status;
};

extern struct event_log events;

struct zzb_node *test_node(void);

/* Resets every fake and the event log */
void reset_fakes(void);

/* Initializes BDB again, on the network EXT_PAN_ID when on_network is set */
void reboot(bool on_network);

/* Runs the stack thread until it waits, without advancing the time */
void run_stack(void);

#endif /* ZZB_TESTS_UNIT_BDB_FAKES_H_ */
