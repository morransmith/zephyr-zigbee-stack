/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>

#include "aps/aps.h"
#include "common/node.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.6.1.1) */
#define REVISION 3

/* Commands received (ZCL Table 3-37); each response has the identifier of its command */
#define CMD_ADD_GROUP                0x00
#define CMD_VIEW_GROUP               0x01
#define CMD_GET_GROUP_MEMBERSHIP     0x02
#define CMD_REMOVE_GROUP             0x03
#define CMD_REMOVE_ALL_GROUPS        0x04
#define CMD_ADD_GROUP_IF_IDENTIFYING 0x05

/* Capacity, group count, then the group list */
#define MEMBERSHIP_LIST_MAX ((ZZB_ZCL_MAX_PAYLOAD - 2) / 2)

/* Capacity of Get Group Membership Response that means at least one more */
#define CAPACITY_MAX 0xfd

static const struct zzb_zcl_attr attrs[] = {
	/* NameSupport: group names are not supported */
	ZZB_ZCL_ATTR_CONST(0x0000, ZZB_ZCL_MAP8, 0),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static bool valid(uint16_t group)
{
	return group >= 0x0001 && group <= 0xfff7;
}

/* Sends the response id with status and group to a unicast command (ZCL 3.6.2.3.1) */
static int respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t status,
		   uint16_t group)
{
	uint8_t rsp[3] = {status};

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	sys_put_le16(group, rsp + 1);
	zzb_zcl_cmd_respond(node, cmd, cmd->id, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.6.2.3.2 steps 1 to 4; the group name is not stored */
static uint8_t add(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	if (!valid(group)) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return zzb_apsme_add_group(node, group, ep) ? ZZB_ZCL_INSUFFICIENT_SPACE : ZZB_ZCL_SUCCESS;
}

/* ZCL 3.6.2.3.3; the name of a group is the empty string */
static int view_group(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint16_t group)
{
	uint8_t rsp[4] = {ZZB_ZCL_SUCCESS};

	if (!valid(group)) {
		rsp[0] = ZZB_ZCL_INVALID_VALUE;
	} else if (!zzb_aps_group_member(node, group, cmd->ep)) {
		rsp[0] = ZZB_ZCL_NOT_FOUND;
	}

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	sys_put_le16(group, rsp + 1);
	zzb_zcl_cmd_respond(node, cmd, CMD_VIEW_GROUP, rsp, sizeof(rsp));
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.6.2.3.4 */
static int get_group_membership(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t groups[MEMBERSHIP_LIST_MAX];
	uint8_t rsp[2 + 2 * MEMBERSHIP_LIST_MAX];
	uint8_t count;
	int n = 0;

	if (cmd->len < 1 || cmd->len < 1 + 2 * cmd->payload[0]) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	count = cmd->payload[0];
	if (count == 0) {
		n = zzb_aps_groups_of(node, cmd->ep, groups, ARRAY_SIZE(groups));
	}

	for (int i = 0; i < count && n < ARRAY_SIZE(groups); i++) {
		uint16_t group = sys_get_le16(cmd->payload + 1 + 2 * i);

		if (zzb_aps_group_member(node, group, cmd->ep)) {
			groups[n++] = group;
		}
	}

	/* Without a match only a unicast is answered */
	if (count > 0 && n == 0 && !cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	rsp[0] = MIN(zzb_aps_group_capacity(node), CAPACITY_MAX);
	rsp[1] = n;
	for (int i = 0; i < n; i++) {
		sys_put_le16(groups[i], rsp + 2 + 2 * i);
	}

	zzb_zcl_cmd_respond(node, cmd, CMD_GET_GROUP_MEMBERSHIP, rsp, 2 + 2 * n);
	return ZZB_ZCL_RESPONDED;
}

/* ZCL 3.6.2.3.5 */
static int remove_group(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint16_t group)
{
	uint8_t status = ZZB_ZCL_SUCCESS;

	if (!valid(group)) {
		status = ZZB_ZCL_INVALID_VALUE;
	} else if (zzb_apsme_remove_group(node, group, cmd->ep)) {
		status = ZZB_ZCL_NOT_FOUND;
	} else {
		zzb_zcl_scenes_remove_group(node, cmd->ep, group);
	}

	return respond(node, cmd, status, group);
}

/* ZCL 3.6.2.3.6: the scenes of every group go too; scenes without a group stay */
static void remove_all_groups(struct zzb_node *node, uint8_t ep)
{
	uint16_t groups[CONFIG_ZZB_APS_GROUP_COUNT];
	int count = zzb_aps_groups_of(node, ep, groups, ARRAY_SIZE(groups));

	for (int i = 0; i < count; i++) {
		zzb_zcl_scenes_remove_group(node, ep, groups[i]);
	}

	zzb_apsme_remove_all_groups(node, ep);
}

static bool identifying(struct zzb_node *node, uint8_t ep)
{
	const struct zzb_zcl_cluster *cl =
		zzb_zcl_find_cluster(zzb_node_endpoint(node, ep), ZZB_ZCL_IDENTIFY, true);
	const struct zzb_zcl_identify *v = cl != NULL ? cl->values : NULL;

	return v != NULL && v->identify_time > 0;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	uint16_t group;

	if (cmd->id == CMD_GET_GROUP_MEMBERSHIP) {
		return get_group_membership(node, cmd);
	}

	/* Remove All Groups always succeeds */
	if (cmd->id == CMD_REMOVE_ALL_GROUPS) {
		remove_all_groups(node, cmd->ep);
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->id > CMD_ADD_GROUP_IF_IDENTIFYING) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	group = sys_get_le16(cmd->payload);
	switch (cmd->id) {
	case CMD_ADD_GROUP:
		return respond(node, cmd, add(node, cmd->ep, group), group);
	case CMD_VIEW_GROUP:
		return view_group(node, cmd, group);
	case CMD_REMOVE_GROUP:
		return remove_group(node, cmd, group);
	default:
		/* ZCL 3.6.2.3.7: errors go in the Default Response */
		return identifying(node, cmd->ep) ? add(node, cmd->ep, group) : ZZB_ZCL_SUCCESS;
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_groups_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = cmd,
};
