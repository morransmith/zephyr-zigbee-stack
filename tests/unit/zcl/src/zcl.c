/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* ZCL 2.5.12.2: SUCCESS answers a command with Disable Default Response 0 */
ZTEST(zzb_zcl, test_cluster_command)
{
	static const uint8_t frame[] = {0x01, 0x21, 0x02, 0xaa, 0xbb};
	static const uint8_t rsp[] = {0x18, 0x21, 0x0b, 0x02, 0x00};

	receive(TEST_CLUSTER, frame, sizeof(frame));
	zassert_equal(cmd_calls.count, 1);
	zassert_equal(cmd_calls.cmd.ep, 1);
	zassert_equal(cmd_calls.cmd.cluster, TEST_CLUSTER);
	zassert_equal(cmd_calls.cmd.src_addr, REQUESTER_ADDR);
	zassert_equal(cmd_calls.cmd.src_ep, REQUESTER_EP);
	zassert_equal(cmd_calls.cmd.tsn, 0x21);
	zassert_equal(cmd_calls.cmd.id, 0x02);
	zassert_equal(cmd_calls.cmd.len, 2);
	zassert_mem_equal(cmd_calls.payload, frame + 3, 2);
	zassert_equal_ptr(cmd_calls.values, &values1);
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.4.1.1.4: with Disable Default Response 1 only an error gets a Default Response */
ZTEST(zzb_zcl, test_disable_default_response)
{
	static const uint8_t frame[] = {0x11, 0x22, 0x02};
	static const uint8_t rsp[] = {0x18, 0x22, 0x0b, 0x02, 0x85};

	receive(TEST_CLUSTER, frame, sizeof(frame));
	zassert_equal(cmd_calls.count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	cmd_status = 0x85;
	receive(TEST_CLUSTER, frame, sizeof(frame));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* A hook that sent its own response gets no Default Response */
ZTEST(zzb_zcl, test_own_response)
{
	cmd_status = ZZB_ZCL_RESPONDED;
	receive(TEST_CLUSTER, (uint8_t[]){0x01, 0x23, 0x02}, 3);
	zassert_equal(cmd_calls.count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* ZCL 2.5.12.2: the endpoint has no instance for the cluster and the direction */
ZTEST(zzb_zcl, test_unsupported_cluster)
{
	static const uint8_t no_cluster[] = {0x18, 0x24, 0x0b, 0x00, 0xc3};
	static const uint8_t no_client[] = {0x10, 0x25, 0x0b, 0x00, 0xc3};
	static const uint8_t no_server[] = {0x18, 0x26, 0x0b, 0x01, 0xc3};

	receive(0x0300, (uint8_t[]){0x01, 0x24, 0x00}, 3);
	check_frame(0, 1, 0x0300, no_cluster, sizeof(no_cluster));

	/* Server to client, but the test cluster is a server */
	receive(TEST_CLUSTER, (uint8_t[]){0x09, 0x25, 0x00}, 3);
	check_frame(1, 1, TEST_CLUSTER, no_client, sizeof(no_client));

	/* Client to server, but On/Off is a client */
	receive(ON_OFF, (uint8_t[]){0x01, 0x26, 0x01}, 3);
	check_frame(2, 1, ON_OFF, no_server, sizeof(no_server));
	zassert_equal(cmd_calls.count, 0);
}

/* ZCL 2.3.3, 2.5.12.1.1: the response carries the manufacturer code of the command */
ZTEST(zzb_zcl, test_manufacturer_specific)
{
	static const uint8_t frame[] = {0x05, 0x34, 0x12, 0x27, 0x02};
	static const uint8_t rsp[] = {0x1c, 0x34, 0x12, 0x27, 0x0b, 0x02, 0x81};

	receive(TEST_CLUSTER, frame, sizeof(frame));
	zassert_equal(cmd_calls.count, 0);
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.5.12.2: UNSUP_COMMAND, also for a cluster without a definition */
ZTEST(zzb_zcl, test_unsupported_commands)
{
	/* Discover Commands Received */
	static const uint8_t discover_commands[] = {0x00, 0x28, 0x11, 0x00, 0x05};
	static const uint8_t rsp[] = {0x18, 0x28, 0x0b, 0x11, 0x81};
	static const uint8_t client_rsp[] = {0x10, 0x29, 0x0b, 0x01, 0x81};

	receive(TEST_CLUSTER, discover_commands, sizeof(discover_commands));
	check_frame(0, 1, TEST_CLUSTER, rsp, sizeof(rsp));

	receive(ON_OFF, (uint8_t[]){0x09, 0x29, 0x01}, 3);
	check_frame(1, 1, ON_OFF, client_rsp, sizeof(client_rsp));
}

ZTEST(zzb_zcl, test_dropped_frames)
{
	struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = 1,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = 0x0300,
	};

	/* A received Default Response is never answered (ZCL 2.5.12.2) */
	receive(0x0300, (uint8_t[]){0x18, 0x30, 0x0b, 0x00, 0x00}, 5);

	/* No Default Response to a NWK broadcast */
	ind.dst_addr = 0xfffd;
	receive_ind(&ind, (uint8_t[]){0x01, 0x31, 0x00}, 3);
	ind.cluster = TEST_CLUSTER;
	receive_ind(&ind, (uint8_t[]){0x01, 0x32, 0x02}, 3);
	zassert_equal(cmd_calls.count, 1);

	/* An endpoint that the device does not have, another profile */
	ind.dst_addr = NODE_ADDR;
	ind.dst_ep = 9;
	receive_ind(&ind, (uint8_t[]){0x01, 0x33, 0x02}, 3);
	ind.dst_ep = 1;
	ind.profile = 0x0109;
	receive_ind(&ind, (uint8_t[]){0x01, 0x34, 0x02}, 3);

	/* A short frame and a reserved frame type */
	receive(TEST_CLUSTER, (uint8_t[]){0x01, 0x35}, 2);
	receive(TEST_CLUSTER, (uint8_t[]){0x02, 0x36, 0x02}, 3);

	zassert_equal(cmd_calls.count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* The wildcard profile reaches every endpoint */
ZTEST(zzb_zcl, test_wildcard_profile)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = 2,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0xffff,
		.cluster = TEST_CLUSTER,
	};
	static const uint8_t rsp[] = {0x18, 0x37, 0x0b, 0x02, 0x00};

	receive_ind(&ind, (uint8_t[]){0x01, 0x37, 0x02}, 3);
	zassert_equal_ptr(cmd_calls.values, &values2);
	check_frame(0, 2, TEST_CLUSTER, rsp, sizeof(rsp));
}

/* ZCL 2.3.2.1: endpoints with the cluster get the frame, and no Default Response follows */
ZTEST(zzb_zcl, test_broadcast_endpoint)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = 0xff,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = TEST_CLUSTER,
	};
	struct zzb_apsde_ind no_cluster = ind;

	cmd_status = 0x85;
	receive_ind(&ind, (uint8_t[]){0x01, 0x38, 0x02}, 3);
	zassert_equal(cmd_calls.count, 2);
	zassert_equal(cmd_calls.cmd.ep, 2);
	zassert_equal_ptr(cmd_calls.values, &values2);

	no_cluster.cluster = 0x0300;
	receive_ind(&no_cluster, (uint8_t[]){0x01, 0x39, 0x02}, 3);
	zassert_equal(cmd_calls.count, 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* R22 2.2.8.4.2: member endpoints with the cluster get a group frame, without Default Response */
ZTEST(zzb_zcl, test_group_frame)
{
	struct zzb_apsde_ind ind = {
		.dst_addr = 0x1234,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = TEST_CLUSTER,
		.group = true,
	};

	memberships[0] = (struct membership){.group = 0x1234, .ep = 2};
	memberships[1] = (struct membership){.group = 0x1234, .ep = 3};
	memberships[2] = (struct membership){.group = 0x4321, .ep = 1};
	cmd_status = 0x85;
	receive_ind(&ind, (uint8_t[]){0x01, 0x3a, 0x02}, 3);
	zassert_equal(cmd_calls.count, 1);
	zassert_equal(cmd_calls.cmd.ep, 2);
	zassert_equal_ptr(cmd_calls.values, &values2);

	ind.dst_addr = 0x5555;
	receive_ind(&ind, (uint8_t[]){0x01, 0x3b, 0x02}, 3);
	zassert_equal(cmd_calls.count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* A command of finding & binding: a broadcast without APS ACK, a unicast with it */
ZTEST(zzb_zcl, test_send_cmd)
{
	uint8_t tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_send_cmd(test_node(), 1, 0xffff, 0xff, 0x0003, 0x01, NULL, 0));
	zassert_equal(sent[0].req.dst_mode, ZZB_APS_ADDR_SHORT);
	zassert_equal(sent[0].req.dst_addr, 0xffff);
	zassert_equal(sent[0].req.dst_ep, 0xff);
	zassert_equal(sent[0].req.src_ep, 1);
	zassert_equal(sent[0].req.profile, 0x0104);
	zassert_equal(sent[0].req.cluster, 0x0003);
	zassert_equal(sent[0].req.tx_options, 0);
	zassert_equal(sent[0].len, 3);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x11, tsn, 0x01}), 3);

	zassert_ok(zzb_zcl_send_cmd(test_node(), 1, 0x1234, 0x02, 0x0004, 0x05,
				    (uint8_t[]){0x01, 0x00, 0x00}, 3));
	zassert_equal(sent[1].req.tx_options, ZZB_APS_TX_ACK);
	zassert_mem_equal(sent[1].data, ((uint8_t[]){0x11, tsn + 1, 0x05, 0x01, 0x00, 0x00}), 6);

	zassert_equal(zzb_zcl_send_cmd(test_node(), 9, 0xffff, 0xff, 0x0003, 0x01, NULL, 0),
		      -ENOENT);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
}

ZTEST_SUITE(zzb_zcl, NULL, setup, before, after, NULL);
