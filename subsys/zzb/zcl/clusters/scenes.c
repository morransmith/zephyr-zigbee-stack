/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zzb/zcl/scenes.h>

#include "aps/aps.h"
#include "common/node.h"
#include "storage/settings.h"
#include "zcl/frame.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.7.1.1) */
#define REVISION 3

/* Commands received (ZCL Table 3-42); each response has the identifier of its command */
#define CMD_ADD_SCENE            0x00
#define CMD_VIEW_SCENE           0x01
#define CMD_REMOVE_SCENE         0x02
#define CMD_REMOVE_ALL_SCENES    0x03
#define CMD_STORE_SCENE          0x04
#define CMD_RECALL_SCENE         0x05
#define CMD_GET_SCENE_MEMBERSHIP 0x06
#define CMD_ENHANCED_ADD_SCENE   0x40
#define CMD_ENHANCED_VIEW_SCENE  0x41
#define CMD_COPY_SCENE           0x42

/* Mode of Copy Scene */
#define COPY_ALL_SCENES BIT(0)

/* Capacity of Get Scene Membership Response that means at least one more */
#define CAPACITY_MAX 0xfd

/* Cluster ID and length of an extension field set */
#define SET_HDR_LEN 3

/* Group ID, scene ID and transition time of Add Scene */
#define ADD_FIELDS_LEN 5

#define RECORD_VERSION 1
/* Version, endpoint, group, scene ID, transition time and TransitionTime100ms */
#define RECORD_HDR_LEN 8
/* Room for zcl/scene/<i> with any int i */
#define NAME_LEN       22

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_SCENES_SCENE_COUNT, ZZB_ZCL_UINT8, 0, struct zzb_zcl_scenes,
		     scene_count),
	ZZB_ZCL_ATTR(ZZB_ZCL_SCENES_CURRENT_SCENE, ZZB_ZCL_UINT8, 0, struct zzb_zcl_scenes,
		     current_scene),
	ZZB_ZCL_ATTR(ZZB_ZCL_SCENES_CURRENT_GROUP, ZZB_ZCL_UINT16, 0, struct zzb_zcl_scenes,
		     current_group),
	ZZB_ZCL_ATTR(ZZB_ZCL_SCENES_SCENE_VALID, ZZB_ZCL_BOOL, 0, struct zzb_zcl_scenes,
		     scene_valid),
	/* NameSupport: scene names are not supported */
	ZZB_ZCL_ATTR_CONST(0x0004, ZZB_ZCL_MAP8, 0),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint16_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_SCENES, attr, value);
}

static struct zzb_zcl_scene *find(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id)
{
	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		if (s->in_use && s->ep == ep && s->group == group && s->id == id) {
			return s;
		}
	}

	return NULL;
}

/* The entry of the scene, or a free one; NULL when the table is full */
static struct zzb_zcl_scene *entry_for(struct zzb_node *node, uint8_t ep, uint16_t group,
				       uint8_t id)
{
	struct zzb_zcl_scene *s = find(node, ep, group, id);

	if (s != NULL) {
		return s;
	}

	ARRAY_FOR_EACH_PTR(node->zcl.scenes, free) {
		if (!free->in_use) {
			return free;
		}
	}

	return NULL;
}

/* SceneCount follows every change of the table */
static void count(struct zzb_node *node, uint8_t ep)
{
	int n = 0;

	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		n += s->in_use && s->ep == ep;
	}

	set(node, ep, ZZB_ZCL_SCENES_SCENE_COUNT, n);
}

static void record_name(char name[NAME_LEN], int index)
{
	snprintk(name, NAME_LEN, "zcl/scene/%d", index);
}

/* Writes scene to the entry s and saves it; a failed save is ignored */
static void put(struct zzb_node *node, struct zzb_zcl_scene *s, const struct zzb_zcl_scene *scene)
{
	uint8_t rec[RECORD_HDR_LEN + CONFIG_ZZB_ZCL_SCENE_DATA_LEN] = {RECORD_VERSION, scene->ep};
	char name[NAME_LEN];

	*s = *scene;
	sys_put_le16(s->group, rec + 2);
	rec[4] = s->id;
	sys_put_le16(s->transition, rec + 5);
	rec[7] = s->transition_100ms;
	memcpy(rec + RECORD_HDR_LEN, s->data, s->data_len);
	record_name(name, s - node->zcl.scenes);
	zzb_settings_save(node, name, rec, RECORD_HDR_LEN + s->data_len);
}

static void remove_scene(struct zzb_node *node, struct zzb_zcl_scene *s)
{
	char name[NAME_LEN];

	s->in_use = false;
	record_name(name, s - node->zcl.scenes);
	zzb_settings_delete(node, name);
}

/* ZCL 3.7.2.4.1: the range of the group, then the membership of the endpoint */
static uint8_t check_group(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	if (group > 0xfff7) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	if (group != 0 && !zzb_aps_group_member(node, group, ep)) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	return ZZB_ZCL_SUCCESS;
}

/* Whether the extension field sets at p fill exactly len octets */
static bool sets_valid(const uint8_t *p, size_t len)
{
	while (len >= SET_HDR_LEN && len >= SET_HDR_LEN + p[2]) {
		len -= SET_HDR_LEN + p[2];
		p += SET_HDR_LEN + p[2];
	}

	return len == 0;
}

/* The values of the set of cluster in the valid sets at p, with their length, or NULL */
static const uint8_t *find_set(const uint8_t *p, size_t len, uint16_t cluster, size_t *set_len)
{
	while (len > 0) {
		if (sys_get_le16(p) == cluster) {
			*set_len = p[2];
			return p + SET_HDR_LEN;
		}

		len -= SET_HDR_LEN + p[2];
		p += SET_HDR_LEN + p[2];
	}

	return NULL;
}

/* The length of the leading attributes with the scene flag of cl that len octets hold */
static size_t cut(const struct zzb_zcl_cluster *cl, size_t len)
{
	size_t n = 0;

	for (int i = 0; cl->def != NULL && i < cl->def->attr_count; i++) {
		const struct zzb_zcl_attr *attr = &cl->def->attrs[i];

		if (!(attr->flags & ZZB_ZCL_SCENE)) {
			continue;
		}

		if (n + ZZB_ZCL_STORAGE_SIZE(attr->type) > len) {
			break;
		}

		n += ZZB_ZCL_STORAGE_SIZE(attr->type);
	}

	return n;
}

/*
 * Copies the sets at p of the server clusters on ep into s, in the order of the endpoint, each cut
 * to the attributes of its cluster. Returns false when they do not fit.
 */
static bool copy_sets(const struct zzb_endpoint *ep, const uint8_t *p, size_t len,
		      struct zzb_zcl_scene *s)
{
	s->data_len = 0;
	for (int i = 0; i < ep->cluster_count; i++) {
		const struct zzb_zcl_cluster *cl = &ep->clusters[i];
		size_t have = 0;
		const uint8_t *set = cl->server ? find_set(p, len, cl->id, &have) : NULL;
		size_t take = set != NULL ? cut(cl, have) : 0;

		if (take == 0) {
			continue;
		}

		if (s->data_len + SET_HDR_LEN + take > sizeof(s->data)) {
			return false;
		}

		sys_put_le16(cl->id, s->data + s->data_len);
		s->data[s->data_len + 2] = take;
		memcpy(s->data + s->data_len + SET_HDR_LEN, set, take);
		s->data_len += SET_HDR_LEN + take;
	}

	return true;
}

/* Adds or overwrites a scene with the sets at p; space is checked only for a new entry */
static uint8_t add(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id, uint16_t time,
		   bool tenths, const uint8_t *p, size_t len)
{
	struct zzb_zcl_scene *s = entry_for(node, ep, group, id);
	struct zzb_zcl_scene scene = {
		.in_use = true,
		.ep = ep,
		.group = group,
		.id = id,
		.transition = tenths ? time / 10 : time,
		.transition_100ms = tenths ? time % 10 : 0,
	};

	if (s == NULL || !copy_sets(zzb_node_endpoint(node, ep), p, len, &scene)) {
		return ZZB_ZCL_INSUFFICIENT_SPACE;
	}

	put(node, s, &scene);
	count(node, ep);
	return ZZB_ZCL_SUCCESS;
}

/* Writes the sets of the current values of the server clusters on ep; false if they do not fit */
static bool current_sets(const struct zzb_endpoint *ep, struct zzb_zcl_scene *s)
{
	s->data_len = 0;
	for (int i = 0; i < ep->cluster_count; i++) {
		const struct zzb_zcl_cluster *cl = &ep->clusters[i];
		size_t len = cl->server ? cut(cl, SIZE_MAX) : 0;
		uint8_t *p = s->data + s->data_len + SET_HDR_LEN;

		if (len == 0) {
			continue;
		}

		if (s->data_len + SET_HDR_LEN + len > sizeof(s->data)) {
			return false;
		}

		sys_put_le16(cl->id, s->data + s->data_len);
		s->data[s->data_len + 2] = len;
		for (int j = 0; j < cl->def->attr_count; j++) {
			const struct zzb_zcl_attr *attr = &cl->def->attrs[j];

			if (attr->flags & ZZB_ZCL_SCENE) {
				p += zzb_zcl_attr_encode(cl, attr, p,
							 ZZB_ZCL_STORAGE_SIZE(attr->type));
			}
		}

		s->data_len += SET_HDR_LEN + len;
	}

	return true;
}

/* Writes the values of a set of cl, which may lack trailing attributes */
static void recall_set(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		       const uint8_t *p, size_t len)
{
	for (int i = 0; i < cl->def->attr_count; i++) {
		const struct zzb_zcl_attr *attr = &cl->def->attrs[i];
		size_t size = ZZB_ZCL_STORAGE_SIZE(attr->type);

		if (!(attr->flags & ZZB_ZCL_SCENE)) {
			continue;
		}

		if (size > len) {
			break;
		}

		zzb_zcl_set(node, ep, cl->id, attr->id, size == 2 ? sys_get_le16(p) : p[0]);
		p += size;
		len -= size;
	}

	if (cl->def->recalled != NULL) {
		cl->def->recalled(node, ep, cl->values);
	}
}

/* The scene becomes the current one (ZCL 3.7.2.2.1) */
static void make_current(struct zzb_node *node, const struct zzb_zcl_scene *s)
{
	set(node, s->ep, ZZB_ZCL_SCENES_CURRENT_SCENE, s->id);
	set(node, s->ep, ZZB_ZCL_SCENES_CURRENT_GROUP, s->group);
	set(node, s->ep, ZZB_ZCL_SCENES_SCENE_VALID, true);
}

/* Sends the response with status, the group and the scene ID to a unicast (ZCL 3.7.2.4.1) */
static int respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t status,
		   uint16_t group, uint8_t id)
{
	uint8_t rsp[4] = {status};

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	sys_put_le16(group, rsp + 1);
	rsp[3] = id;
	zzb_zcl_cmd_respond(node, cmd, cmd->id, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.7.2.4.2, 3.7.2.4.9; the name is not stored */
static int add_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	const uint8_t *p = cmd->payload;
	int name_len = cmd->len >= ADD_FIELDS_LEN
			       ? zzb_zcl_value_len(ZZB_ZCL_STRING, p + ADD_FIELDS_LEN,
						   cmd->len - ADD_FIELDS_LEN)
			       : -EINVAL;
	size_t sets_off = ADD_FIELDS_LEN + name_len;
	uint16_t group;
	uint8_t status;

	if (name_len < 0 || !sets_valid(p + sets_off, cmd->len - sets_off)) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	group = sys_get_le16(p);
	status = check_group(node, cmd->ep, group);
	if (status == ZZB_ZCL_SUCCESS) {
		status = add(node, cmd->ep, group, p[2], sys_get_le16(p + 3),
			     cmd->id == CMD_ENHANCED_ADD_SCENE, p + sets_off, cmd->len - sets_off);
	}

	return respond(node, cmd, status, group, p[2]);
}

/*
 * ZCL 3.7.2.4.3, 3.7.2.4.10; the name of a scene is the empty string. Enhanced View Scene gives the
 * transition time in tenths of a second.
 */
static int view_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint32_t tenths;

	uint8_t rsp[7 + CONFIG_ZZB_ZCL_SCENE_DATA_LEN];
	uint16_t group = sys_get_le16(cmd->payload);
	uint8_t id = cmd->payload[2];
	const struct zzb_zcl_scene *s = find(node, cmd->ep, group, id);

	rsp[0] = check_group(node, cmd->ep, group);
	if (rsp[0] == ZZB_ZCL_SUCCESS && s == NULL) {
		rsp[0] = ZZB_ZCL_NOT_FOUND;
	}

	if (rsp[0] != ZZB_ZCL_SUCCESS || !cmd->unicast) {
		return respond(node, cmd, rsp[0], group, id);
	}

	tenths = 10 * s->transition + s->transition_100ms;
	sys_put_le16(group, rsp + 1);
	rsp[3] = id;
	sys_put_le16(cmd->id == CMD_ENHANCED_VIEW_SCENE ? MIN(tenths, UINT16_MAX) : s->transition,
		     rsp + 4);
	rsp[6] = 0;
	memcpy(rsp + 7, s->data, s->data_len);
	zzb_zcl_cmd_respond(node, cmd, cmd->id, rsp, 7 + s->data_len);
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.7.2.4.4 */
static int remove_one(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t group = sys_get_le16(cmd->payload);
	uint8_t id = cmd->payload[2];
	struct zzb_zcl_scene *s = find(node, cmd->ep, group, id);
	uint8_t status = check_group(node, cmd->ep, group);

	if (status == ZZB_ZCL_SUCCESS && s == NULL) {
		status = ZZB_ZCL_NOT_FOUND;
	}

	if (status == ZZB_ZCL_SUCCESS) {
		remove_scene(node, s);
		count(node, cmd->ep);
	}

	return respond(node, cmd, status, group, id);
}

/* ZCL 3.7.2.4.6: a new entry has transition time 0; an existing one keeps it */
uint8_t zzb_zcl_scene_store(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id)
{
	struct zzb_zcl_scene *s = entry_for(node, ep, group, id);
	struct zzb_zcl_scene scene = {
		.in_use = true,
		.ep = ep,
		.group = group,
		.id = id,
	};

	if (s == NULL || !current_sets(zzb_node_endpoint(node, ep), &scene)) {
		return ZZB_ZCL_INSUFFICIENT_SPACE;
	}

	if (s->in_use) {
		scene.transition = s->transition;
		scene.transition_100ms = s->transition_100ms;
	}

	put(node, s, &scene);
	count(node, ep);
	make_current(node, s);
	return ZZB_ZCL_SUCCESS;
}

static int store_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t group = sys_get_le16(cmd->payload);
	uint8_t status = check_group(node, cmd->ep, group);

	if (status == ZZB_ZCL_SUCCESS) {
		status = zzb_zcl_scene_store(node, cmd->ep, group, cmd->payload[2]);
	}

	return respond(node, cmd, status, group, cmd->payload[2]);
}

/* ZCL 3.7.2.4.7: a cluster without a set keeps its state */
uint8_t zzb_zcl_scene_recall(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);
	const struct zzb_zcl_scene *s = find(node, ep, group, id);
	const uint8_t *p;

	if (s == NULL) {
		return ZZB_ZCL_NOT_FOUND;
	}

	for (p = s->data; p < s->data + s->data_len; p += SET_HDR_LEN + p[2]) {
		const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(e, sys_get_le16(p), true);

		if (cl != NULL && cl->def != NULL) {
			recall_set(node, ep, cl, p + SET_HDR_LEN, p[2]);
		}
	}

	make_current(node, s);
	return ZZB_ZCL_SUCCESS;
}

/*
 * OnOff switches at once, so the transition time field is not read. The status goes in the Default
 * Response.
 */
static int recall_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t group = sys_get_le16(cmd->payload);
	uint8_t status = check_group(node, cmd->ep, group);

	if (status != ZZB_ZCL_SUCCESS) {
		return status;
	}

	return zzb_zcl_scene_recall(node, cmd->ep, group, cmd->payload[2]);
}

/* Copies the scene from to the scene id of group; false when the table is full */
static bool copy(struct zzb_node *node, const struct zzb_zcl_scene *from, uint16_t group,
		 uint8_t id)
{
	struct zzb_zcl_scene *to = entry_for(node, from->ep, group, id);
	struct zzb_zcl_scene scene = *from;

	if (to == NULL) {
		return false;
	}

	scene.group = group;
	scene.id = id;
	put(node, to, &scene);
	return true;
}

/* A missing source gives INVALID_FIELD (ZCL Table 3-44) */
static uint8_t copy_one(struct zzb_node *node, uint8_t ep, uint16_t from, uint8_t from_id,
			uint16_t to, uint8_t to_id)
{
	const struct zzb_zcl_scene *s = find(node, ep, from, from_id);

	if (s == NULL) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	return copy(node, s, to, to_id) ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INSUFFICIENT_SPACE;
}

/* The scenes keep their IDs; those copied before the table fills stay (ZCL 3.7.2.5.9.4) */
static uint8_t copy_all(struct zzb_node *node, uint8_t ep, uint16_t from, uint16_t to)
{
	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		if (s->in_use && s->ep == ep && s->group == from && !copy(node, s, to, s->id)) {
			return ZZB_ZCL_INSUFFICIENT_SPACE;
		}
	}

	return ZZB_ZCL_SUCCESS;
}

/* ZCL 3.7.2.4.11 */
static int copy_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	const uint8_t *p = cmd->payload;
	uint16_t from = sys_get_le16(p + 1);
	uint16_t to = sys_get_le16(p + 4);
	uint8_t status = check_group(node, cmd->ep, from);

	if (status == ZZB_ZCL_SUCCESS) {
		status = check_group(node, cmd->ep, to);
	}

	if (status == ZZB_ZCL_SUCCESS) {
		status = (p[0] & COPY_ALL_SCENES) ? copy_all(node, cmd->ep, from, to)
						  : copy_one(node, cmd->ep, from, p[3], to, p[6]);
		count(node, cmd->ep);
	}

	return respond(node, cmd, status, from, p[3]);
}

void zzb_zcl_scenes_invalidate(struct zzb_node *node, uint8_t ep, bool notify)
{
	if (notify) {
		set(node, ep, ZZB_ZCL_SCENES_SCENE_VALID, false);
	} else {
		zzb_zcl_store(node, ep, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_SCENE_VALID, false);
	}
}

void zzb_zcl_scenes_remove_group(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		if (s->in_use && s->ep == ep && s->group == group) {
			remove_scene(node, s);
		}
	}

	count(node, ep);
}

/* ZCL 3.7.2.4.5 */
static int remove_all(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t group = sys_get_le16(cmd->payload);
	uint8_t rsp[3] = {check_group(node, cmd->ep, group)};

	if (rsp[0] == ZZB_ZCL_SUCCESS) {
		zzb_zcl_scenes_remove_group(node, cmd->ep, group);
	}

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	sys_put_le16(group, rsp + 1);
	zzb_zcl_cmd_respond(node, cmd, CMD_REMOVE_ALL_SCENES, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.7.2.4.8: the capacity is the space left for all groups */
static int get_scene_membership(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint8_t rsp[5 + CONFIG_ZZB_ZCL_SCENE_COUNT];
	uint16_t group = sys_get_le16(cmd->payload);
	size_t n = 5;

	rsp[0] = check_group(node, cmd->ep, group);
	rsp[1] = 0;
	sys_put_le16(group, rsp + 2);
	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		if (!s->in_use) {
			rsp[1]++;
		} else if (s->ep == cmd->ep && s->group == group) {
			rsp[n++] = s->id;
		}
	}

	rsp[1] = MIN(rsp[1], CAPACITY_MAX);
	rsp[4] = n - 5;

	/* Only a unicast is answered without a scene of the group */
	if (!cmd->unicast && (rsp[0] != ZZB_ZCL_SUCCESS || n == 5)) {
		return ZZB_ZCL_SUCCESS;
	}

	zzb_zcl_cmd_respond(node, cmd, CMD_GET_SCENE_MEMBERSHIP, rsp,
			    rsp[0] == ZZB_ZCL_SUCCESS ? n : 4);
	return ZZB_ZCL_RESPONDED;
}

/* The commands with the length of their Group ID and scene ID fields */
static const struct {
	uint8_t id;
	uint8_t len;
	int (*fn)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd);
} commands[] = {
	{CMD_ADD_SCENE, 3, add_scene},
	{CMD_VIEW_SCENE, 3, view_scene},
	{CMD_REMOVE_SCENE, 3, remove_one},
	{CMD_REMOVE_ALL_SCENES, 2, remove_all},
	{CMD_STORE_SCENE, 3, store_scene},
	{CMD_RECALL_SCENE, 3, recall_scene},
	{CMD_GET_SCENE_MEMBERSHIP, 2, get_scene_membership},
	{CMD_ENHANCED_ADD_SCENE, 3, add_scene},
	{CMD_ENHANCED_VIEW_SCENE, 3, view_scene},
	{CMD_COPY_SCENE, 7, copy_scene},
};

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	ARRAY_FOR_EACH(commands, i) {
		if (commands[i].id != cmd->id) {
			continue;
		}

		if (cmd->len < commands[i].len) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		return commands[i].fn(node, cmd);
	}

	return ZZB_ZCL_UNSUP_COMMAND;
}

/* The scenes of the endpoint go, and the attributes take their defaults */
static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		if (s->in_use && s->ep == ep) {
			remove_scene(node, s);
		}
	}

	set(node, ep, ZZB_ZCL_SCENES_SCENE_COUNT, 0);
	set(node, ep, ZZB_ZCL_SCENES_CURRENT_SCENE, 0);
	set(node, ep, ZZB_ZCL_SCENES_CURRENT_GROUP, 0);
	set(node, ep, ZZB_ZCL_SCENES_SCENE_VALID, false);
}

static void init(void *values)
{
	*(struct zzb_zcl_scenes *)values = (struct zzb_zcl_scenes){0};
}

/* SceneCount of the loaded table, without zcl_cb */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	int n = 0;

	ARRAY_FOR_EACH_PTR(node->zcl.scenes, s) {
		n += s->in_use && s->ep == ep;
	}

	zzb_zcl_store(node, ep, ZZB_ZCL_SCENES, ZZB_ZCL_SCENES_SCENE_COUNT, n);
}

int zzb_zcl_scenes_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			void *cb_arg)
{
	uint8_t rec[RECORD_HDR_LEN + CONFIG_ZZB_ZCL_SCENE_DATA_LEN];
	const struct zzb_endpoint *ep;
	struct zzb_zcl_scene *s;
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->zcl.scenes)) {
		return 0;
	}

	if (len < RECORD_HDR_LEN || len > sizeof(rec)) {
		return -EINVAL;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, len);
	if (err) {
		return err;
	}

	/* A record of an endpoint without a Scenes server is ignored */
	ep = zzb_node_endpoint(node, rec[1]);
	if (ep == NULL || zzb_zcl_find_cluster(ep, ZZB_ZCL_SCENES, true) == NULL) {
		return 0;
	}

	s = &node->zcl.scenes[index];
	*s = (struct zzb_zcl_scene){
		.in_use = true,
		.ep = rec[1],
		.group = sys_get_le16(rec + 2),
		.id = rec[4],
		.transition = sys_get_le16(rec + 5),
		.transition_100ms = rec[7],
		.data_len = len - RECORD_HDR_LEN,
	};
	memcpy(s->data, rec + RECORD_HDR_LEN, s->data_len);
	return 0;
}

void zzb_zcl_scenes_delete(struct zzb_node *node)
{
	char name[NAME_LEN];

	for (int i = 0; i < ARRAY_SIZE(node->zcl.scenes); i++) {
		record_name(name, i);
		zzb_settings_delete(node, name);
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_scenes_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.cmd = cmd,
};
