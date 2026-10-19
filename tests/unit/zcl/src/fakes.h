/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZCL_FAKES_H_
#define ZZB_TESTS_UNIT_ZCL_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>
#include <zzb/zcl.h>
#include <zzb/zcl/basic.h>

#include "aps/aps.h"
#include "common/buf.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);

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

/* An On/Off client without a definition on endpoints 1 and 3 */
#define ON_OFF 0x0006

/*
 * Values of the test cluster, attributes 0x0000 to 0x0007 in this order; 0x0002 is saved. The
 * reset sets 0x0001 and 0x0002 to 0.
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
};

/* ClusterRevision of the test cluster */
#define TEST_REVISION 1

extern struct test_values values1;
extern struct test_values values2;

/* The Basic server on endpoint 2 */
extern struct zzb_zcl_basic basic;

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
	uint8_t ep[8];
	uint16_t attr[8];
};

extern struct changed_calls hook_calls;

struct zcl_cb_calls {
	int count;
	uint8_t ep[8];
	uint16_t cluster[8];
	uint16_t attr[8];
};

extern struct zcl_cb_calls zcl_cb_calls;

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

/* Loads the settings records of the node again, on the stack thread */
int reboot(void);

/* Resets the ZCL attributes as the reset of the node does, on the stack thread */
void reset_node(void);

/* Checks that frame i is a ZCL frame of cluster from endpoint ep to the requester with the ASDU */
void check_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len);

#endif /* ZZB_TESTS_UNIT_ZCL_FAKES_H_ */
