/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/appliance_events.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 15.4.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 15.4.2.3 */
static const struct zzb_zcl_command server_commands[] = {
	{ZZB_ZCL_APPLIANCE_EVENTS_GET_ALERTS},
};

/* ZCL 15.4.2.4 */
static const struct zzb_zcl_command client_commands[] = {
	/* Alerts Count, with the number of alerts in bits 0 to 3, and the alert structures */
	{ZZB_ZCL_APPLIANCE_EVENTS_GET_ALERTS_RSP, {{3, 0x0f}}},
	{ZZB_ZCL_APPLIANCE_EVENTS_ALERTS_NOTIFICATION, {{3, 0x0f}}},
	/* Event Header, Event Identification */
	{ZZB_ZCL_APPLIANCE_EVENTS_EVENT_NOTIFICATION, {{2}}},
};

static int server_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, server_commands, ARRAY_SIZE(server_commands));
}

static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, client_commands, ARRAY_SIZE(client_commands));
}

const struct zzb_zcl_cluster_def zzb_zcl_appliance_events_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = server_cmd,
};

const struct zzb_zcl_cluster_def zzb_zcl_appliance_events_client_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = client_cmd,
};
