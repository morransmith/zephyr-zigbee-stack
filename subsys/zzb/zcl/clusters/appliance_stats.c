/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/appliance_stats.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 15.5.1.1) */
#define REVISION 1

/* Log Notification and Log Response: Time Stamp and Log ID, then Log Length, a uint32 */
#define LOG_LENGTH_OFFSET 8
#define LOG_HDR_LEN       (LOG_LENGTH_OFFSET + 4)

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_STATS_LOG_MAX_SIZE, ZZB_ZCL_UINT32, 0,
		     struct zzb_zcl_appliance_stats, log_max_size),
	ZZB_ZCL_ATTR(ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_MAX_SIZE, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_appliance_stats, log_queue_max_size),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 15.5.3.2 */
static const struct zzb_zcl_command server_commands[] = {
	/* Log ID */
	{ZZB_ZCL_APPLIANCE_STATS_LOG_REQUEST, {{4}}},
	{ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_REQUEST},
};

/* ZCL 15.5.2.2 */
static const struct zzb_zcl_command client_commands[] = {
	/* Time Stamp, Log ID and Log Length, then the log payload, which client_cmd() checks */
	{ZZB_ZCL_APPLIANCE_STATS_LOG_NOTIFICATION, {{LOG_HDR_LEN}}},
	{ZZB_ZCL_APPLIANCE_STATS_LOG_RSP, {{LOG_HDR_LEN}}},
	/* Log Queue Size and the log IDs */
	{ZZB_ZCL_APPLIANCE_STATS_LOG_QUEUE_RSP, {{4, 0xff}}},
	{ZZB_ZCL_APPLIANCE_STATS_STATISTICS_AVAILABLE, {{4, 0xff}}},
};

static int server_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, server_commands, ARRAY_SIZE(server_commands));
}

/* Whether a log command holds fewer octets of log payload than its Log Length (ZCL 15.5.2.2.1) */
static bool log_short(const struct zzb_zcl_cmd *cmd)
{
	if (cmd->id != ZZB_ZCL_APPLIANCE_STATS_LOG_NOTIFICATION &&
	    cmd->id != ZZB_ZCL_APPLIANCE_STATS_LOG_RSP) {
		return false;
	}

	return cmd->len >= LOG_HDR_LEN &&
	       cmd->len - LOG_HDR_LEN < sys_get_le32(cmd->payload + LOG_LENGTH_OFFSET);
}

static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (log_short(cmd)) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	return zzb_zcl_pass(node, cmd, client_commands, ARRAY_SIZE(client_commands));
}

const struct zzb_zcl_cluster_def zzb_zcl_appliance_stats_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.cmd = server_cmd,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_appliance_stats_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = client_cmd,
};
