/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/scenes.h>

#include "common/node.h"
#include "fakes.h"

#define ADD_SCENE            0x00
#define VIEW_SCENE           0x01
#define REMOVE_SCENE         0x02
#define REMOVE_ALL_SCENES    0x03
#define STORE_SCENE          0x04
#define RECALL_SCENE         0x05
#define GET_SCENE_MEMBERSHIP 0x06

static void *setup(void)
{
	test_node();
	return NULL;
}

/* Endpoint 2 is a member of group 0x0001 */
static void before(void *fixture)
{
	reset_node();
	reset_fakes();
	memberships[0] = (struct membership){.group = 0x0001, .ep = 2};
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/* Sends the Scenes command id with the payload to endpoint 2 and checks the response in rsp */
static void command(uint8_t id, const uint8_t *payload, size_t len, const uint8_t *rsp,
		    size_t rsp_len)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[64] = {0x01, ++tsn, id};
	uint8_t expected[64] = {0x19, tsn, id};

	memcpy(frame + 3, payload, len);
	memcpy(expected + 3, rsp, rsp_len);
	receive_ep(2, ZZB_ZCL_SCENES, frame, 3 + len);
	check_frame(frames, 2, ZZB_ZCL_SCENES, expected, 3 + rsp_len);
}

/* Sends the Scenes command id with the payload to endpoint 2 and checks its Default Response */
static void command_status(uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	int frames = zzb_apsde_data_req_fake.call_count;
	uint8_t frame[64] = {0x01, ++tsn, id};
	const uint8_t expected[] = {0x18, tsn, 0x0b, id, status};

	memcpy(frame + 3, payload, len);
	receive_ep(2, ZZB_ZCL_SCENES, frame, 3 + len);
	check_frame(frames, 2, ZZB_ZCL_SCENES, expected, sizeof(expected));
}

/* Adds scene id of group with transition time 3 s, the name "ab" and OnOff on */
static void add_scene(uint16_t group, uint8_t id)
{
	const uint8_t req[] = {group & 0xff, group >> 8, id,   0x03, 0x00, 0x02,
			       'a',          'b',        0x06, 0x00, 0x01, 0x01};

	command(ADD_SCENE, req, sizeof(req),
		(uint8_t[]){ZZB_ZCL_SUCCESS, group & 0xff, group >> 8, id}, 4);
}

/* ZCL 3.7.2.2; names are not supported */
ZTEST(zzb_zcl_scenes, test_attributes)
{
	static const uint8_t req[] = {0x00, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02,
				      0x00, 0x03, 0x00, 0x04, 0x00, 0xfd, 0xff};
	static const uint8_t rsp[] = {0x18, 0x80, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x01,
				      0x00, 0x00, 0x20, 0x00, 0x02, 0x00, 0x00, 0x21, 0x00,
				      0x00, 0x03, 0x00, 0x00, 0x10, 0x00, 0x04, 0x00, 0x00,
				      0x18, 0x00, 0xfd, 0xff, 0x00, 0x21, 0x03, 0x00};

	receive_ep(2, ZZB_ZCL_SCENES, req, sizeof(req));
	check_frame(0, 2, ZZB_ZCL_SCENES, rsp, sizeof(rsp));
}

/*
 * ZCL 3.7.2.4.2, 3.7.2.4.3: the name is not stored; the set of a cluster that the endpoint does
 * not have is dropped, and a set is cut to the attributes of its cluster
 */
ZTEST(zzb_zcl_scenes, test_add_and_view)
{
	static const uint8_t add[] = {0x01, 0x00, 0x05, 0x03, 0x00, 0x02, 'a',  'b',  0x00,
				      0x03, 0x02, 0xaa, 0xbb, 0x06, 0x00, 0x02, 0x01, 0x07};

	command(ADD_SCENE, add, sizeof(add), (uint8_t[]){0x00, 0x01, 0x00, 0x05}, 4);
	zassert_equal(scenes.scene_count, 1);
	zassert_equal(zcl_cb_calls.count, 1);
	zassert_equal(zcl_cb_calls.cluster[0], ZZB_ZCL_SCENES);
	zassert_equal(zcl_cb_calls.attr[0], ZZB_ZCL_SCENES_SCENE_COUNT);

	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x05}, 3,
		(uint8_t[]){0x00, 0x01, 0x00, 0x05, 0x03, 0x00, 0x00, 0x06, 0x00, 0x01, 0x01}, 11);
	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x06}, 3, (uint8_t[]){0x8b, 0x01, 0x00, 0x06},
		4);

	/* A scene without extension field sets; the same IDs overwrite the entry */
	command(ADD_SCENE, (uint8_t[]){0x01, 0x00, 0x05, 0x00, 0x00, 0x00}, 6,
		(uint8_t[]){0x00, 0x01, 0x00, 0x05}, 4);
	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x05}, 3,
		(uint8_t[]){0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00}, 7);
	zassert_equal(scenes.scene_count, 1);

	/* A set longer than the frame */
	command_status(ADD_SCENE, (uint8_t[]){0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x02},
		       9, ZZB_ZCL_MALFORMED_COMMAND);
	command_status(ADD_SCENE, (uint8_t[]){0x01, 0x00, 0x05, 0x00, 0x00}, 5,
		       ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 3.7.2.4.1: the group range, then the membership of the endpoint; group 0 needs none */
ZTEST(zzb_zcl_scenes, test_group_checks)
{
	command(ADD_SCENE, (uint8_t[]){0xf8, 0xff, 0x01, 0x00, 0x00, 0x00}, 6,
		(uint8_t[]){0x87, 0xf8, 0xff, 0x01}, 4);
	command(ADD_SCENE, (uint8_t[]){0x02, 0x00, 0x01, 0x00, 0x00, 0x00}, 6,
		(uint8_t[]){0x85, 0x02, 0x00, 0x01}, 4);
	command(VIEW_SCENE, (uint8_t[]){0x02, 0x00, 0x01}, 3, (uint8_t[]){0x85, 0x02, 0x00, 0x01},
		4);
	add_scene(0x0000, 1);
	zassert_equal(scenes.scene_count, 1);
}

/* ZCL 3.7.2.4.4, 3.7.2.4.5 */
ZTEST(zzb_zcl_scenes, test_remove)
{
	add_scene(0x0001, 1);
	add_scene(0x0001, 2);
	add_scene(0x0000, 3);

	command(REMOVE_SCENE, (uint8_t[]){0x01, 0x00, 0x01}, 3, (uint8_t[]){0x00, 0x01, 0x00, 0x01},
		4);
	command(REMOVE_SCENE, (uint8_t[]){0x01, 0x00, 0x01}, 3, (uint8_t[]){0x8b, 0x01, 0x00, 0x01},
		4);
	zassert_equal(scenes.scene_count, 2);

	command(REMOVE_ALL_SCENES, (uint8_t[]){0x01, 0x00}, 2, (uint8_t[]){0x00, 0x01, 0x00}, 3);
	zassert_equal(scenes.scene_count, 1);
	command(REMOVE_ALL_SCENES, (uint8_t[]){0x02, 0x00}, 2, (uint8_t[]){0x85, 0x02, 0x00}, 3);
}

/* Space is checked only for a new entry */
ZTEST(zzb_zcl_scenes, test_full_table)
{
	for (int i = 0; i < CONFIG_ZZB_ZCL_SCENE_COUNT; i++) {
		add_scene(0x0001, i);
	}

	command(ADD_SCENE, (uint8_t[]){0x01, 0x00, 0x20, 0x00, 0x00, 0x00}, 6,
		(uint8_t[]){0x89, 0x01, 0x00, 0x20}, 4);
	add_scene(0x0001, 0);
	zassert_equal(scenes.scene_count, CONFIG_ZZB_ZCL_SCENE_COUNT);
}

/* ZCL 3.7.2.4.8: the capacity is the space left for all groups */
ZTEST(zzb_zcl_scenes, test_get_scene_membership)
{
	const struct zzb_apsde_ind broadcast = {
		.dst_addr = 0xffff,
		.dst_ep = 0xff,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_SCENES,
	};
	uint8_t capacity = CONFIG_ZZB_ZCL_SCENE_COUNT - 3;

	add_scene(0x0001, 5);
	add_scene(0x0001, 1);
	add_scene(0x0000, 1);
	command(GET_SCENE_MEMBERSHIP, (uint8_t[]){0x01, 0x00}, 2,
		(uint8_t[]){0x00, capacity, 0x01, 0x00, 0x02, 0x05, 0x01}, 7);
	command(GET_SCENE_MEMBERSHIP, (uint8_t[]){0x02, 0x00}, 2,
		(uint8_t[]){0x85, capacity, 0x02, 0x00}, 4);

	/* A broadcast is answered only for a group with a scene */
	receive_ind(&broadcast, (uint8_t[]){0x01, 0x81, GET_SCENE_MEMBERSHIP, 0x01, 0x00}, 5);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 6);
	memberships[1] = (struct membership){.group = 0x0003, .ep = 2};
	receive_ind(&broadcast, (uint8_t[]){0x01, 0x82, GET_SCENE_MEMBERSHIP, 0x03, 0x00}, 5);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 6);
}

/* ZCL 3.7.2.4.1: a group or broadcast request gets no response */
ZTEST(zzb_zcl_scenes, test_group_request)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = 0x0001,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_SCENES,
		.group = true,
	};

	receive_ind(&ind, (uint8_t[]){0x01, 0x83, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00}, 9);
	zassert_equal(scenes.scene_count, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* ZCL 3.6.2.3.5, 3.6.2.3.6: Groups removes the scenes of the groups it removes */
ZTEST(zzb_zcl_scenes, test_groups_remove_scenes)
{
	memberships[1] = (struct membership){.group = 0x0002, .ep = 2};
	add_scene(0x0001, 1);
	add_scene(0x0002, 1);
	add_scene(0x0000, 1);

	receive_ep(2, ZZB_ZCL_GROUPS, (uint8_t[]){0x01, 0x84, 0x03, 0x01, 0x00}, 5);
	zassert_equal(scenes.scene_count, 2);
	receive_ep(2, ZZB_ZCL_GROUPS, (uint8_t[]){0x01, 0x85, 0x04}, 3);
	zassert_equal(scenes.scene_count, 1);
}

/* ZCL 3.7.2.4.6: the current state; an existing entry keeps its transition time */
ZTEST(zzb_zcl_scenes, test_store)
{
	on_off.on_off = true;
	command(STORE_SCENE, (uint8_t[]){0x01, 0x00, 0x02}, 3, (uint8_t[]){0x00, 0x01, 0x00, 0x02},
		4);
	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x02}, 3,
		(uint8_t[]){0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x01}, 11);
	zassert_equal(scenes.current_scene, 2);
	zassert_equal(scenes.current_group, 0x0001);
	zassert_true(scenes.scene_valid);

	add_scene(0x0001, 3);
	on_off.on_off = false;
	command(STORE_SCENE, (uint8_t[]){0x01, 0x00, 0x03}, 3, (uint8_t[]){0x00, 0x01, 0x00, 0x03},
		4);
	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x03}, 3,
		(uint8_t[]){0x00, 0x01, 0x00, 0x03, 0x03, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00}, 11);
}

ZTEST(zzb_zcl_scenes, test_store_errors)
{
	command(STORE_SCENE, (uint8_t[]){0x02, 0x00, 0x03}, 3, (uint8_t[]){0x85, 0x02, 0x00, 0x03},
		4);
	for (int i = 0; i < CONFIG_ZZB_ZCL_SCENE_COUNT; i++) {
		add_scene(0x0001, i);
	}

	command(STORE_SCENE, (uint8_t[]){0x01, 0x00, 0x09}, 3, (uint8_t[]){0x89, 0x01, 0x00, 0x09},
		4);
}

/*
 * ZCL 3.7.2.4.7: the attributes of the scene, then the current scene; GlobalSceneControl follows
 * OnOff TRUE (ZCL 3.8.2.2.2). The transition time field is accepted.
 */
ZTEST(zzb_zcl_scenes, test_recall)
{
	add_scene(0x0001, 1);
	on_off.global_scene_control = false;
	zcl_cb_calls.count = 0;
	command_status(RECALL_SCENE, (uint8_t[]){0x01, 0x00, 0x01, 0x0a, 0x00}, 5, ZZB_ZCL_SUCCESS);
	zassert_true(on_off.on_off);
	zassert_true(on_off.global_scene_control);
	zassert_equal(scenes.current_scene, 1);
	zassert_equal(scenes.current_group, 0x0001);
	zassert_true(scenes.scene_valid);
	zassert_equal(zcl_cb_calls.attr[0], ZZB_ZCL_ON_OFF_ON_OFF);
	zassert_equal(zcl_cb_calls.attr[1], ZZB_ZCL_ON_OFF_GLOBAL_SCENE_CONTROL);

	command_status(RECALL_SCENE, (uint8_t[]){0x01, 0x00, 0x09}, 3, ZZB_ZCL_NOT_FOUND);
	command_status(RECALL_SCENE, (uint8_t[]){0x02, 0x00, 0x01}, 3, ZZB_ZCL_INVALID_FIELD);
	command_status(RECALL_SCENE, (uint8_t[]){0x01, 0x00}, 2, ZZB_ZCL_MALFORMED_COMMAND);
}

/* ZCL 3.7.2.4.7: a cluster without a set keeps its state */
ZTEST(zzb_zcl_scenes, test_recall_without_a_set)
{
	command(ADD_SCENE, (uint8_t[]){0x01, 0x00, 0x01, 0x00, 0x00, 0x00}, 6,
		(uint8_t[]){0x00, 0x01, 0x00, 0x01}, 4);
	on_off.on_off = true;
	command_status(RECALL_SCENE, (uint8_t[]){0x01, 0x00, 0x01}, 3, ZZB_ZCL_SUCCESS);
	zassert_true(on_off.on_off);
	zassert_true(scenes.scene_valid);
}

/*
 * ZCL 3.7.2.2.1.4: a change of an attribute of a scene clears SceneValid, with zcl_cb for a remote
 * command and without for zzb_zcl_attr_set()
 */
ZTEST(zzb_zcl_scenes, test_scene_valid)
{
	bool on = true;

	command(STORE_SCENE, (uint8_t[]){0x01, 0x00, 0x02}, 3, (uint8_t[]){0x00, 0x01, 0x00, 0x02},
		4);
	zcl_cb_calls.count = 0;
	receive_ep(2, ZZB_ZCL_ON_OFF, (uint8_t[]){0x11, 0x86, 0x01}, 3);
	zassert_false(scenes.scene_valid);
	zassert_equal(zcl_cb_calls.cluster[0], ZZB_ZCL_SCENES);
	zassert_equal(zcl_cb_calls.attr[0], ZZB_ZCL_SCENES_SCENE_VALID);
	zassert_equal(zcl_cb_calls.attr[1], ZZB_ZCL_ON_OFF_ON_OFF);

	command(STORE_SCENE, (uint8_t[]){0x01, 0x00, 0x02}, 3, (uint8_t[]){0x00, 0x01, 0x00, 0x02},
		4);
	on = false;
	zcl_cb_calls.count = 0;
	zassert_ok(zzb_zcl_attr_set(test_node(), 2, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on));
	zassert_false(scenes.scene_valid);
	zassert_equal(zcl_cb_calls.count, 0);
}

/* The reset of the node clears the table */
ZTEST(zzb_zcl_scenes, test_reset)
{
	add_scene(0x0001, 1);
	reset_node();
	zassert_equal(scenes.scene_count, 0);
	command(VIEW_SCENE, (uint8_t[]){0x01, 0x00, 0x01}, 3, (uint8_t[]){0x8b, 0x01, 0x00, 0x01},
		4);
}

ZTEST_SUITE(zzb_zcl_scenes, NULL, setup, before, after, NULL);
