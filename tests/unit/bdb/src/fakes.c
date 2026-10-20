/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_join_req, struct zzb_node *, uint64_t,
		       enum zzb_nwk_join_method, uint32_t, uint8_t, uint8_t, bool);
DEFINE_FAKE_VALUE_FUNC(uint64_t, zzb_nlme_get_ext_pan_id, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(uint16_t, zzb_nlme_get_short_addr, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_network_discovery_req, struct zzb_node *, uint32_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_reset_req, struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_leave_req, struct zzb_node *, bool);
DEFINE_FAKE_VOID_FUNC(zzb_nwk_poll_hold, struct zzb_node *, enum zzb_nwk_hold, bool);
DEFINE_FAKE_VOID_FUNC(zzb_aps_reset, struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(uint64_t, zzb_apsme_get_tc_addr, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(struct zzb_aps_key_pair *, zzb_aps_key_pair_find, struct zzb_node *,
		       uint64_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_request_key_req, struct zzb_node *, uint64_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_verify_key_req, struct zzb_node *, uint64_t,
		       enum zzb_aps_key_type);
DEFINE_FAKE_VOID_FUNC(zzb_zdo_init, struct zzb_node *);
DEFINE_FAKE_VOID_FUNC(zzb_zcl_reset, struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zcl_identify, struct zzb_node *, uint8_t, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zcl_send_cmd, struct zzb_node *, uint8_t, uint16_t, uint8_t,
		       uint16_t, uint8_t, const uint8_t *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_simple_desc_req, struct zzb_node *, uint16_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_ieee_addr_req, struct zzb_node *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_bind_req, struct zzb_node *, const struct zzb_aps_binding *);

struct zzb_aps_binding bindings[8];
uint8_t sent_cmd[8];

static int bind_copy(struct zzb_node *node, const struct zzb_aps_binding *binding)
{
	zassert_true(zzb_apsme_bind_req_fake.call_count <= ARRAY_SIZE(bindings));
	bindings[zzb_apsme_bind_req_fake.call_count - 1] = *binding;
	return 0;
}

static int send_cmd_copy(struct zzb_node *node, uint8_t src_ep, uint16_t dst_addr, uint8_t dst_ep,
			 uint16_t cluster, uint8_t cmd, const uint8_t *payload, size_t len)
{
	zassert_true(len <= sizeof(sent_cmd));
	memcpy(sent_cmd, payload, len);
	return 0;
}
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_device_annce_req, struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_node_desc_req, struct zzb_node *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_mgmt_permit_joining_req, struct zzb_node *, uint8_t, bool);

struct event_log events;
struct zzb_aps_key_pair tc_pair;
struct set_link_key_calls set_link_key;

int zzb_apsme_set_link_key(struct zzb_node *node, uint64_t addr, const uint8_t key[ZZB_SEC_KEY_LEN],
			   enum zzb_aps_key_attr attr, enum zzb_aps_link_key_type type)
{
	set_link_key.count++;
	set_link_key.addr = addr;
	memcpy(set_link_key.key, key, ZZB_SEC_KEY_LEN);
	set_link_key.attr = attr;
	set_link_key.type = type;
	return 0;
}

static void record_event(struct zzb_node *node, enum zzb_bdb_event event,
			 enum zzb_bdb_status status)
{
	events.count++;
	events.event = event;
	events.status = status;
}

/* Cluster instances without definitions, since ZCL is faked */
static const struct zzb_zcl_cluster identify_server[] = {
	{.id = 0x0003, .server = true},
};

static const struct zzb_zcl_cluster initiator[] = {
	{.id = 0x0003},
	{.id = 0x0006},
	{.id = 0x0402, .server = true},
	{.id = 0x0500},
	{.id = 0x0501, .server = true},
	{.id = 0x0502},
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = IDENTIFY_EP,
		.profile = 0x0104,
		.clusters = identify_server,
		.cluster_count = ARRAY_SIZE(identify_server),
	},
	{
		.id = OTHER_EP,
		.profile = 0x0104,
		.clusters = initiator,
		.cluster_count = ARRAY_SIZE(initiator),
	},
};

/* The power source gives the mains power bit of the capability */
static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

struct zzb_node *test_node(void)
{
	static struct zzb_node *node;
	const struct zzb_node_config cfg = {
		.eui64 = 0x00124b0001020304ULL,
		.device = &device,
		.bdb_cb = record_event,
	};

	if (node == NULL) {
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return node;
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_nlme_join_req);
	RESET_FAKE(zzb_nlme_get_ext_pan_id);
	RESET_FAKE(zzb_nlme_get_short_addr);
	RESET_FAKE(zzb_nlme_network_discovery_req);
	RESET_FAKE(zzb_nlme_reset_req);
	RESET_FAKE(zzb_nlme_leave_req);
	RESET_FAKE(zzb_nwk_poll_hold);
	RESET_FAKE(zzb_aps_reset);
	RESET_FAKE(zzb_apsme_get_tc_addr);
	RESET_FAKE(zzb_aps_key_pair_find);
	RESET_FAKE(zzb_apsme_request_key_req);
	RESET_FAKE(zzb_apsme_verify_key_req);
	RESET_FAKE(zzb_zdo_init);
	RESET_FAKE(zzb_zcl_reset);
	RESET_FAKE(zzb_zcl_identify);
	RESET_FAKE(zzb_zcl_send_cmd);
	RESET_FAKE(zzb_zdo_simple_desc_req);
	RESET_FAKE(zzb_zdo_ieee_addr_req);
	RESET_FAKE(zzb_apsme_bind_req);
	RESET_FAKE(zzb_zdo_device_annce_req);
	RESET_FAKE(zzb_zdo_node_desc_req);
	RESET_FAKE(zzb_zdo_mgmt_permit_joining_req);
	FFF_RESET_HISTORY();
	events = (struct event_log){0};
	set_link_key = (struct set_link_key_calls){0};
	tc_pair = (struct zzb_aps_key_pair){
		.in_use = true,
		.addr = TC_EUI64,
		.attr = ZZB_APS_KEY_PROVISIONAL,
		.type = ZZB_APS_LINK_KEY_GLOBAL,
	};
	memcpy(tc_pair.key, "ZigBeeAlliance09", ZZB_SEC_KEY_LEN);

	zzb_nlme_get_short_addr_fake.return_val = NODE_ADDR;
	zzb_apsme_get_tc_addr_fake.return_val = TC_EUI64;
	zzb_aps_key_pair_find_fake.return_val = &tc_pair;
	zzb_apsme_bind_req_fake.custom_fake = bind_copy;
	zzb_zcl_send_cmd_fake.custom_fake = send_cmd_copy;
	memset(bindings, 0, sizeof(bindings));
}

bool held(void)
{
	return zzb_nwk_poll_hold_fake.arg1_val == ZZB_NWK_HOLD_BDB &&
	       zzb_nwk_poll_hold_fake.arg2_val;
}

static void reboot_handler(struct k_work *work)
{
	zzb_bdb_init(test_node());
}

void reboot(bool on_network)
{
	struct k_work work;

	zzb_nlme_get_ext_pan_id_fake.return_val = on_network ? EXT_PAN_ID : 0;
	zzb_call_sync(&work, reboot_handler);
}

/* The test thread is cooperative and has a lower priority than the stack thread */
void run_stack(void)
{
	k_yield();
}
