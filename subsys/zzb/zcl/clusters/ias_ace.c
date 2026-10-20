/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/ias_ace.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 8.3.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 8.3.2.3 */
static const struct zzb_zcl_command server_commands[] = {
	/* Arm Mode, Arm/Disarm Code, Zone ID */
	{ZZB_ZCL_IAS_ACE_ARM, {{1}, {1, 0xff}, {1}}},
	/* Number of Zones and the zone IDs, Arm/Disarm Code */
	{ZZB_ZCL_IAS_ACE_BYPASS, {{1, 0xff}, {1, 0xff}}},
	{ZZB_ZCL_IAS_ACE_EMERGENCY},
	{ZZB_ZCL_IAS_ACE_FIRE},
	{ZZB_ZCL_IAS_ACE_PANIC},
	{ZZB_ZCL_IAS_ACE_GET_ZONE_ID_MAP},
	{ZZB_ZCL_IAS_ACE_GET_ZONE_INFORMATION, {{1}}},
	{ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS},
	{ZZB_ZCL_IAS_ACE_GET_BYPASSED_ZONE_LIST},
	/* Starting Zone ID, Max Number of Zone IDs, Zone Status Mask Flag, Zone Status Mask */
	{ZZB_ZCL_IAS_ACE_GET_ZONE_STATUS, {{5}}},
};

/* ZCL 8.3.2.4 */
static const struct zzb_zcl_command client_commands[] = {
	/* Arm Notification */
	{ZZB_ZCL_IAS_ACE_ARM_RSP, {{1}}},
	/* Zone ID Map sections 0 to 15 */
	{ZZB_ZCL_IAS_ACE_GET_ZONE_ID_MAP_RSP, {{32}}},
	/* Zone ID, Zone Type, IEEE address, Zone Label */
	{ZZB_ZCL_IAS_ACE_GET_ZONE_INFORMATION_RSP, {{11}, {1, 0xff}}},
	/* Zone ID, Zone Status, Audible Notification, Zone Label */
	{ZZB_ZCL_IAS_ACE_ZONE_STATUS_CHANGED, {{4}, {1, 0xff}}},
	/* Panel Status, Seconds Remaining, Audible Notification, Alarm Status */
	{ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED, {{4}}},
	{ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS_RSP, {{4}}},
	/* Number of Zones and the zone IDs */
	{ZZB_ZCL_IAS_ACE_SET_BYPASSED_ZONE_LIST, {{1, 0xff}}},
	/* Number of Zones and the bypass results */
	{ZZB_ZCL_IAS_ACE_BYPASS_RSP, {{1, 0xff}}},
	/* Zone Status Complete, Number of Zones and the zone IDs with their Zone Status */
	{ZZB_ZCL_IAS_ACE_GET_ZONE_STATUS_RSP, {{1}, {3, 0xff}}},
};

static int server_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, server_commands, ARRAY_SIZE(server_commands));
}

static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, client_commands, ARRAY_SIZE(client_commands));
}

const struct zzb_zcl_cluster_def zzb_zcl_ias_ace_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = server_cmd,
};

const struct zzb_zcl_cluster_def zzb_zcl_ias_ace_client_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = client_cmd,
};
