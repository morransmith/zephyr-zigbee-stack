/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>

#include "common/node.h"
#include "fakes.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/*
 * Sends the Groups command id with the payload to endpoint 2 and checks the response, a cluster
 * command with the identifier and the payload in rsp
 */
static void command(uint8_t id, const uint8_t *payload, size_t len, const uint8_t *rsp,
		    size_t rsp_len)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[48] = {0x01, ++tsn, id};
	uint8_t expected[48] = {0x19, tsn};

	memcpy(frame + 3, payload, len);
	memcpy(expected + 2, rsp, rsp_len);
	receive_ep(2, ZZB_ZCL_GROUPS, frame, 3 + len);
	check_frame(frames, 2, ZZB_ZCL_GROUPS, expected, 2 + rsp_len);
}

/* Sends the Groups command id with the payload to endpoint 2 and checks its Default Response */
static void command_status(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[48] = {0x01, ++tsn, id};
	const uint8_t expected[] = {0x18, tsn, 0x0b, id, status};

	memcpy(frame + 3, payload, len);
	receive_ep(2, ZZB_ZCL_GROUPS, frame, 3 + len);
	check_frame(frames, 2, ZZB_ZCL_GROUPS, expected, sizeof(expected));
}

/* ZCL 3.6.2.2: names are not supported; ClusterRevision 3 */
ZTEST(zzb_zcl_groups, test_attributes)
{
	static const uint8_t rsp[] = {0x18, 0x70, 0x01, 0x00, 0x00, 0x00, 0x18,
				      0x00, 0xfd, 0xff, 0x00, 0x21, 0x03, 0x00};

	receive_ep(2, ZZB_ZCL_GROUPS, (uint8_t[]){0x00, 0x70, 0x00, 0x00, 0x00, 0xfd, 0xff}, 7);
	check_frame(0, 2, ZZB_ZCL_GROUPS, rsp, sizeof(rsp));
}

/* ZCL 3.6.2.3.2: the name is not stored; a group already present gives SUCCESS */
ZTEST(zzb_zcl_groups, test_add_group)
{
	command(0x00, (uint8_t[]){0x01, 0x00, 0x03, 'a', 'b', 'c'}, 6,
		(uint8_t[]){0x00, 0x00, 0x01, 0x00}, 4);
	zassert_true(zzb_aps_group_member(test_node(), 0x0001, 2));
	command(0x00, (uint8_t[]){0x01, 0x00, 0x00}, 3, (uint8_t[]){0x00, 0x00, 0x01, 0x00}, 4);

	/* Outside 0x0001 to 0xfff7 */
	command(0x00, (uint8_t[]){0x00, 0x00, 0x00}, 3, (uint8_t[]){0x00, 0x87, 0x00, 0x00}, 4);
	command(0x00, (uint8_t[]){0xf8, 0xff, 0x00}, 3, (uint8_t[]){0x00, 0x87, 0xf8, 0xff}, 4);

	for (int i = 1; i < CONFIG_ZZB_APS_GROUP_COUNT; i++) {
		zassert_ok(zzb_apsme_add_group(test_node(), 0x0100 + i, 1));
	}

	command(0x00, (uint8_t[]){0x02, 0x00, 0x00}, 3, (uint8_t[]){0x00, 0x89, 0x02, 0x00}, 4);

	/* Without the Group ID */
	command_status(0x00, (uint8_t[]){0x02}, 1, ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 3.6.2.3.3: the name of a group is the empty string */
ZTEST(zzb_zcl_groups, test_view_group)
{
	zassert_ok(zzb_apsme_add_group(test_node(), 0x1234, 2));
	command(0x01, (uint8_t[]){0x34, 0x12}, 2, (uint8_t[]){0x01, 0x00, 0x34, 0x12, 0x00}, 5);
	command(0x01, (uint8_t[]){0x35, 0x12}, 2, (uint8_t[]){0x01, 0x8b, 0x35, 0x12, 0x00}, 5);
	command(0x01, (uint8_t[]){0x00, 0x00}, 2, (uint8_t[]){0x01, 0x87, 0x00, 0x00, 0x00}, 5);
}

/* ZCL 3.6.2.3.4 */
ZTEST(zzb_zcl_groups, test_get_group_membership)
{
	const struct zzb_apsde_ind broadcast = {
		.dst_addr = 0xffff,
		.dst_ep = 0xff,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_GROUPS,
	};
	int capacity = CONFIG_ZZB_APS_GROUP_COUNT - 3;

	zassert_ok(zzb_apsme_add_group(test_node(), 0x0001, 2));
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0003, 1));
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0002, 2));

	/* Every group of the endpoint */
	command(0x02, (uint8_t[]){0x00}, 1,
		(uint8_t[]){0x02, capacity, 0x02, 0x01, 0x00, 0x02, 0x00}, 7);

	/* The groups of the request that the endpoint has */
	command(0x02, (uint8_t[]){0x03, 0x02, 0x00, 0x03, 0x00, 0x09, 0x00}, 7,
		(uint8_t[]){0x02, capacity, 0x01, 0x02, 0x00}, 5);

	/* None of them: a unicast gets count 0, a broadcast nothing */
	command(0x02, (uint8_t[]){0x01, 0x09, 0x00}, 3, (uint8_t[]){0x02, capacity, 0x00}, 3);
	receive_ind(&broadcast, (uint8_t[]){0x01, 0x71, 0x02, 0x01, 0x09, 0x00}, 6);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);

	/* A broadcast with count 0 is answered */
	receive_ind(&broadcast, (uint8_t[]){0x01, 0x72, 0x02, 0x00}, 4);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);

	command_status(0x02, (uint8_t[]){0x02, 0x01, 0x00}, 3, ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 3.6.2.3.5 */
ZTEST(zzb_zcl_groups, test_remove_group)
{
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0001, 2));
	command(0x03, (uint8_t[]){0x01, 0x00}, 2, (uint8_t[]){0x03, 0x00, 0x01, 0x00}, 4);
	zassert_false(zzb_aps_group_member(test_node(), 0x0001, 2));
	command(0x03, (uint8_t[]){0x01, 0x00}, 2, (uint8_t[]){0x03, 0x8b, 0x01, 0x00}, 4);
	command(0x03, (uint8_t[]){0xff, 0xff}, 2, (uint8_t[]){0x03, 0x87, 0xff, 0xff}, 4);
}

/* ZCL 3.6.2.3.6: the groups of the endpoint; no response of its own */
ZTEST(zzb_zcl_groups, test_remove_all_groups)
{
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0001, 2));
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0002, 2));
	zassert_ok(zzb_apsme_add_group(test_node(), 0x0001, 1));
	command_status(0x04, NULL, 0, ZZB_ZCL_SUCCESS);
	zassert_false(zzb_aps_group_member(test_node(), 0x0001, 2));
	zassert_false(zzb_aps_group_member(test_node(), 0x0002, 2));
	zassert_true(zzb_aps_group_member(test_node(), 0x0001, 1));
}

/* ZCL 3.6.2.3.7: only while the endpoint identifies; errors as Default Response */
ZTEST(zzb_zcl_groups, test_add_group_if_identifying)
{
	uint16_t time = 10;

	command_status(0x05, (uint8_t[]){0x01, 0x00, 0x00}, 3, ZZB_ZCL_SUCCESS);
	zassert_false(zzb_aps_group_member(test_node(), 0x0001, 2));

	zassert_ok(
		zzb_zcl_attr_set(test_node(), 2, ZZB_ZCL_IDENTIFY, ZZB_ZCL_IDENTIFY_TIME, &time));
	command_status(0x05, (uint8_t[]){0x01, 0x00, 0x00}, 3, ZZB_ZCL_SUCCESS);
	zassert_true(zzb_aps_group_member(test_node(), 0x0001, 2));
	command_status(0x05, (uint8_t[]){0x00, 0x00, 0x00}, 3, ZZB_ZCL_INVALID_VALUE);
}

/* ZCL 3.6.2.3.1: a group or broadcast request gets no response */
ZTEST(zzb_zcl_groups, test_group_requests)
{
	struct zzb_apsde_ind ind = {
		.dst_addr = 0x0001,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_GROUPS,
		.group = true,
	};

	memberships[0] = (struct membership){.group = 0x0001, .ep = 2};
	receive_ind(&ind, (uint8_t[]){0x01, 0x73, 0x00, 0x05, 0x00, 0x00}, 6);
	zassert_true(zzb_aps_group_member(test_node(), 0x0005, 2));
	receive_ind(&ind, (uint8_t[]){0x01, 0x74, 0x01, 0x05, 0x00}, 5);
	receive_ind(&ind, (uint8_t[]){0x01, 0x75, 0x03, 0x05, 0x00}, 5);
	zassert_false(zzb_aps_group_member(test_node(), 0x0005, 2));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* zzb_zcl_groups_add_group() sends Add Group from the client on endpoint 5 */
ZTEST(zzb_zcl_groups, test_send_add_group)
{
	uint8_t next_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_groups_add_group(test_node(), CLIENT_EP, 0x1234, "ab", 2));
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_GROUPS);
	zassert_equal(sent[0].len, 8);
	zassert_mem_equal(sent[0].data,
			  ((uint8_t[]){0x11, next_tsn, 0x00, 0x34, 0x12, 0x02, 'a', 'b'}), 8);

	/* A name longer than 16 characters is refused */
	zassert_equal(
		zzb_zcl_groups_add_group(test_node(), CLIENT_EP, 0x1234, "abcdefghijklmnopq", 17),
		-EINVAL);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
}

/* zzb_zcl_groups_remove_group() sends Remove Group from the client on endpoint 5 */
ZTEST(zzb_zcl_groups, test_send_remove_group)
{
	uint8_t next_tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_groups_remove_group(test_node(), CLIENT_EP, 0x1234));
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_GROUPS);
	zassert_equal(sent[0].len, 5);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x11, next_tsn, 0x03, 0x34, 0x12}), 5);
}

ZTEST_SUITE(zzb_zcl_groups, NULL, setup, before, after, NULL);
