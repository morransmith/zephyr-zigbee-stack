/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/scenes.h>

#include "aps/aps.h"
#include "common/node.h"
#include "zcl/frame.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.7.1.1) */
#define REVISION 3

/* Commands received (ZCL Table 3-42); each response has the identifier of its command */
#define CMD_ADD_SCENE            0x00
#define CMD_VIEW_SCENE           0x01
#define CMD_REMOVE_SCENE         0x02
#define CMD_REMOVE_ALL_SCENES    0x03
#define CMD_GET_SCENE_MEMBERSHIP 0x06

/* Capacity of Get Scene Membership Response that means at least one more */
#define CAPACITY_MAX 0xfd

/* Cluster ID and length of an extension field set */
#define SET_HDR_LEN 3

/* Group ID, scene ID and transition time of Add Scene */
#define ADD_FIELDS_LEN 5

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

static void remove_scene(struct zzb_node *node, struct zzb_zcl_scene *s)
{
	s->in_use = false;
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
static uint8_t add(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id,
		   uint16_t transition, const uint8_t *p, size_t len)
{
	struct zzb_zcl_scene *s = entry_for(node, ep, group, id);
	struct zzb_zcl_scene scene = {
		.in_use = true,
		.ep = ep,
		.group = group,
		.id = id,
		.transition = transition,
	};

	if (s == NULL || !copy_sets(zzb_node_endpoint(node, ep), p, len, &scene)) {
		return ZZB_ZCL_INSUFFICIENT_SPACE;
	}

	*s = scene;
	count(node, ep);
	return ZZB_ZCL_SUCCESS;
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

/* ZCL 3.7.2.4.2; the name is not stored */
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
		status = add(node, cmd->ep, group, p[2], sys_get_le16(p + 3), p + sets_off,
			     cmd->len - sets_off);
	}

	return respond(node, cmd, status, group, p[2]);
}

/* ZCL 3.7.2.4.3; the name of a scene is the empty string */
static int view_scene(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
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

	sys_put_le16(group, rsp + 1);
	rsp[3] = id;
	sys_put_le16(s->transition, rsp + 4);
	rsp[6] = 0;
	memcpy(rsp + 7, s->data, s->data_len);
	zzb_zcl_cmd_respond(node, cmd, CMD_VIEW_SCENE, rsp, 7 + s->data_len);
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
	{CMD_GET_SCENE_MEMBERSHIP, 2, get_scene_membership},
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

const struct zzb_zcl_cluster_def zzb_zcl_scenes_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
};
