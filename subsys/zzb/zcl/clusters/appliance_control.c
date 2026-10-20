/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zzb/zcl/appliance_control.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 15.2.1.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_CONTROL_START_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_appliance_control, start_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_CONTROL_FINISH_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_appliance_control, finish_time),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_CONTROL_REMAINING_TIME, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_appliance_control, remaining_time),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 15.2.4 */
static const struct zzb_zcl_command server_commands[] = {
	/* Command Identification */
	{ZZB_ZCL_APPLIANCE_CONTROL_EXECUTION_OF_COMMAND, {{1}}},
	{ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE},
	/* The records, which records_valid() checks */
	{ZZB_ZCL_APPLIANCE_CONTROL_WRITE_FUNCTIONS},
	{ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_PAUSE_RESUME},
	{ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_PAUSE},
	/* Warning Event, an enum8, although ZCL Figure 15-5 gives 2 octets */
	{ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_WARNING, {{1}}},
};

/* ZCL 15.2.5 */
static const struct zzb_zcl_command client_commands[] = {
	/* Appliance Status, Remote Enable Flags; Appliance Status 2 may follow */
	{ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE_RSP, {{2}}},
	{ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE_NOTIFICATION, {{2}}},
};

/*
 * Whether the payload of Write Functions consists of whole records: function identifier, data
 * type and data of the length of the type (ZCL 15.2.4.3.1)
 */
static bool records_valid(const struct zzb_zcl_cmd *cmd)
{
	const uint8_t *p = cmd->payload;
	size_t left = cmd->len;

	while (left > 0) {
		int len = left >= 3 ? zzb_zcl_value_len(p[2], p + 3, left - 3) : -EINVAL;

		if (len < 0) {
			return false;
		}

		p += 3 + len;
		left -= 3 + len;
	}

	return true;
}

static int server_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (cmd->id == ZZB_ZCL_APPLIANCE_CONTROL_WRITE_FUNCTIONS && !records_valid(cmd)) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	return zzb_zcl_pass(node, cmd, server_commands, ARRAY_SIZE(server_commands));
}

static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, client_commands, ARRAY_SIZE(client_commands));
}

const struct zzb_zcl_cluster_def zzb_zcl_appliance_control_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = server_cmd,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_appliance_control_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = client_cmd,
};
