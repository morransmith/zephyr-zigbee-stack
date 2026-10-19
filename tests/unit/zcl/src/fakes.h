/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZCL_FAKES_H_
#define ZZB_TESTS_UNIT_ZCL_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>
#include <zzb/zcl.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>

#include "aps/aps.h"
#include "common/buf.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_end, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_query_rsp, struct zzb_node *, uint8_t, uint16_t, uint8_t);

#define NODE_EUI64 0x00124b0001020304ULL
#define NODE_ADDR  0x5678

/* The coordinator sends the requests from its endpoint 1 */
#define REQUESTER_ADDR 0x0000
#define REQUESTER_EP   0x01

/*
 * A cluster of the test: a server on endpoints 1 and 2, not on endpoint 3. Endpoint 2 also has the
 * servers of the stack.
 */
#define TEST_CLUSTER 0x0f00

/*
 * An On/Off client without a definition on endpoints 1 and 3; endpoint 3 also has a server.
 * Endpoint 1 has the Identify client of the stack.
 */
#define ON_OFF ZZB_ZCL_ON_OFF

/*
 * Values of the test cluster, attributes 0x0000 to 0x000b in this order; 0x0000 to 0x0002 and
 * 0x0008 are reportable, 0x0002 and 0x0008 to 0x000a are saved, 0x000b is read-only. The reset sets
 * 0x0001 and 0x0002 to 0.
 */
struct test_values {
	bool b;
	uint8_t u8;
	uint16_t u16;
	uint8_t e8;
	uint8_t map8;
	const char *str;
	const uint8_t *octstr;
	const char *null_str;
	int16_t i16;
	uint32_t u24;
	uint32_t map32;
	uint16_t map16;
};

/* ClusterRevision of the test cluster */
#define TEST_REVISION 1

extern struct test_values values1;
extern struct test_values values2;

/* The Basic, Identify, Groups, Scenes and On/Off servers on endpoint 2 */
extern struct zzb_zcl_basic basic;
extern struct zzb_zcl_identify identify;
extern struct zzb_zcl_scenes scenes;
extern struct zzb_zcl_on_off on_off;

/* The On/Off server on endpoint 3, which has no Scenes server */
extern struct zzb_zcl_on_off on_off3;

/* Calls of the cmd hook of the test cluster, with a copy of the last command */
struct cmd_calls {
	int count;
	struct zzb_zcl_cmd cmd;
	uint8_t payload[ZZB_BUF_SIZE];
	void *values;
};

extern struct cmd_calls cmd_calls;

/* The value the cmd hook returns, ZZB_ZCL_SUCCESS after reset_fakes() */
extern int cmd_status;

/* The check hook of the test cluster rejects a value of attribute 0x0003 above this */
#define TEST_E8_MAX 0x10

/* Calls of the changed hook of the test cluster and of zcl_cb, in order */
struct changed_calls {
	int count;
	uint8_t ep[64];
	uint16_t attr[64];
};

extern struct changed_calls hook_calls;

struct zcl_cb_calls {
	int count;
	uint8_t ep[64];
	uint16_t cluster[64];
	uint16_t attr[64];
};

extern struct zcl_cb_calls zcl_cb_calls;

/* Calls of zcl_effect_cb, with the arguments of the last one */
struct effect_calls {
	int count;
	uint8_t ep;
	uint16_t cluster;
	uint8_t effect;
	uint8_t variant;
	/* zcl_cb_calls.count at the call */
	int zcl_cb_count;
};

extern struct effect_calls effect_calls;

/* A membership of the APS group table, kept by the fakes of the group functions; ep 0 is free */
struct membership {
	uint16_t group;
	uint8_t ep;
};

extern struct membership memberships[CONFIG_ZZB_APS_GROUP_COUNT];

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

/* Passes the ASDU to ZCL as APSDE-DATA.indication with the parameters of ind */
void receive_ind(const struct zzb_apsde_ind *ind, const uint8_t *asdu, size_t len);

/* Passes a ZCL frame of cluster from the requester to endpoint ep as unicast, profile 0x0104 */
void receive_ep(uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len);

/* receive_ep() to endpoint 1 */
void receive(uint16_t cluster, const uint8_t *asdu, size_t len);

/* Starts ZCL again from the settings records, as zzb_node_init() does, on the stack thread */
int reboot(void);

/* Resets the ZCL attributes as the reset of the node does, on the stack thread */
void reset_node(void);

/* Checks that frame i is a ZCL frame of cluster from endpoint ep to the requester with the ASDU */
void check_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len);

#endif /* ZZB_TESTS_UNIT_ZCL_FAKES_H_ */
