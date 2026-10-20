/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_BDB_FAKES_H_
#define ZZB_TESTS_UNIT_BDB_FAKES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zzb/bdb.h>

#include "aps/aps.h"
#include "nwk/nwk.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_join_req, struct zzb_node *, uint64_t,
			enum zzb_nwk_join_method, uint32_t, uint8_t, uint8_t, bool);
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_nlme_get_ext_pan_id, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint16_t, zzb_nlme_get_short_addr, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_network_discovery_req, struct zzb_node *, uint32_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_reset_req, struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_nlme_leave_req, struct zzb_node *, bool);
DECLARE_FAKE_VOID_FUNC(zzb_nwk_poll_hold, struct zzb_node *, enum zzb_nwk_hold, bool);
DECLARE_FAKE_VOID_FUNC(zzb_aps_reset, struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_apsme_get_tc_addr, const struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(struct zzb_aps_key_pair *, zzb_aps_key_pair_find, struct zzb_node *,
			uint64_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_request_key_req, struct zzb_node *, uint64_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_verify_key_req, struct zzb_node *, uint64_t,
			enum zzb_aps_key_type);
DECLARE_FAKE_VOID_FUNC(zzb_zdo_init, struct zzb_node *);
DECLARE_FAKE_VOID_FUNC(zzb_zcl_reset, struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zcl_identify, struct zzb_node *, uint8_t, uint16_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zcl_send_cmd, struct zzb_node *, uint8_t, uint16_t, uint8_t,
			uint16_t, uint8_t, const uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_simple_desc_req, struct zzb_node *, uint16_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_ieee_addr_req, struct zzb_node *, uint16_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_apsme_bind_req, struct zzb_node *, const struct zzb_aps_binding *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_device_annce_req, struct zzb_node *);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_node_desc_req, struct zzb_node *, uint16_t);
DECLARE_FAKE_VALUE_FUNC(int, zzb_zdo_mgmt_permit_joining_req, struct zzb_node *, uint8_t, bool);

#define EXT_PAN_ID 0x00124b0000001a62ULL
#define NODE_ADDR  0x5678

/* nwkCapabilityInformation: allocate address, receiver on when idle, mains power */
#define CAPABILITY 0x8c

/* The capability of a sleepy node: allocate address, mains power */
#define SLEEPY_CAPABILITY 0x84

#define TC_EUI64 0x00124b0000000001ULL

/*
 * Endpoint 1 has an Identify server. Endpoint 2 is an initiator with an Identify client, an On/Off
 * client and a Temperature Measurement server.
 */
#define IDENTIFY_EP 1
#define OTHER_EP    2

/* The bindings of the zzb_apsme_bind_req() fake */
extern struct zzb_aps_binding bindings[8];

/* The payload of the last zzb_zcl_send_cmd() */
extern uint8_t sent_cmd[8];

/* The trust center entry that zzb_aps_key_pair_find() returns, with the default key */
extern struct zzb_aps_key_pair tc_pair;

/*
 * Calls of the zzb_apsme_set_link_key() fake. FFF declares the array parameter as a pointer, which
 * -Werror=array-parameter rejects.
 */
struct set_link_key_calls {
	int count;
	uint64_t addr;
	uint8_t key[ZZB_SEC_KEY_LEN];
	enum zzb_aps_key_attr attr;
	enum zzb_aps_link_key_type type;
};

extern struct set_link_key_calls set_link_key;

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

/* Whether the last call of the zzb_nwk_poll_hold() fake set the hold of BDB */
bool held(void);

/* Initializes BDB again, on the network EXT_PAN_ID when on_network is set */
void reboot(bool on_network);

/* Runs the stack thread until it waits, without advancing the time */
void run_stack(void);

#endif /* ZZB_TESTS_UNIT_BDB_FAKES_H_ */
