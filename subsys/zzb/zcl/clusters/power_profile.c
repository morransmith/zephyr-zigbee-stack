/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/power_profile.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.17.1.1) */
#define REVISION 1

/* The bits of ScheduleMode that ZCL Table 3-137 defines */
#define SCHEDULE_MODE_BITS 0x03

/* Options and PowerProfileID of GetPowerProfilePriceExtended, then PowerProfileStartTime */
#define PRICE_EXTENDED_LEN 4

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_PROFILE_TOTAL_PROFILE_NUM, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_power_profile, total_profile_num),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_PROFILE_MULTIPLE_SCHEDULING, ZZB_ZCL_BOOL, 0,
		     struct zzb_zcl_power_profile, multiple_scheduling),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_PROFILE_ENERGY_FORMATTING, ZZB_ZCL_MAP8, 0,
		     struct zzb_zcl_power_profile, energy_formatting),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_PROFILE_ENERGY_REMOTE, ZZB_ZCL_BOOL, 0,
		     struct zzb_zcl_power_profile, energy_remote),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE, ZZB_ZCL_MAP8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_power_profile,
		     schedule_mode),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 3.17.5 */
static const struct zzb_zcl_command server_commands[] = {
	/* PowerProfileID */
	{ZZB_ZCL_POWER_PROFILE_REQUEST, {{1}}},
	{ZZB_ZCL_POWER_PROFILE_STATE_REQUEST},
	/* PowerProfileID, Currency, Price, PriceTrailingDigit */
	{ZZB_ZCL_POWER_PROFILE_GET_PRICE_RSP, {{8}}},
	/* Currency, Price, PriceTrailingDigit */
	{ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE_RSP, {{7}}},
	/* PowerProfileID, Num of Scheduled Phases, EnergyPhaseID and ScheduledTime of each phase */
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_NOTIFICATION, {{1}, {3, 0xff}}},
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_RSP, {{1}, {3, 0xff}}},
	{ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_REQUEST, {{1}}},
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_REQUEST, {{1}}},
	{ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED_RSP, {{8}}},
};

/* ZCL 3.17.6 */
static const struct zzb_zcl_command client_commands[] = {
	/* TotalProfileNum, PowerProfileID, Num of Transferred Phases and 10 octets of each phase */
	{ZZB_ZCL_POWER_PROFILE_NOTIFICATION, {{2}, {10, 0xff}}},
	{ZZB_ZCL_POWER_PROFILE_RSP, {{2}, {10, 0xff}}},
	/* Power Profile Count and the records */
	{ZZB_ZCL_POWER_PROFILE_STATE_RSP, {{4, 0xff}}},
	/* PowerProfileID */
	{ZZB_ZCL_POWER_PROFILE_GET_PRICE, {{1}}},
	{ZZB_ZCL_POWER_PROFILE_STATE_NOTIFICATION, {{4, 0xff}}},
	{ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE},
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_REQUEST, {{1}}},
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_RSP, {{1}, {3, 0xff}}},
	{ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_STATE_NOTIFICATION, {{1}, {3, 0xff}}},
	/* PowerProfileID, StartAfter, StopBefore */
	{ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_NOTIFICATION, {{5}}},
	{ZZB_ZCL_POWER_PROFILE_SCHEDULE_CONSTRAINTS_RSP, {{5}}},
	/* Options, PowerProfileID */
	{ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED, {{2}}},
};

static int server_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return zzb_zcl_pass(node, cmd, server_commands, ARRAY_SIZE(server_commands));
}

/* ZCL 3.17.6.12.1: bit 0 of Options announces PowerProfileStartTime */
static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (cmd->id == ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED && cmd->len > 0 &&
	    (cmd->payload[0] & ZZB_ZCL_POWER_PROFILE_START_TIME_PRESENT) &&
	    cmd->len < PRICE_EXTENDED_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	return zzb_zcl_pass(node, cmd, client_commands, ARRAY_SIZE(client_commands));
}

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	if (attr->id == ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE && (value & ~SCHEDULE_MODE_BITS)) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

/* The fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_power_profile *v = values;

	v->schedule_mode = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_POWER_PROFILE, ZZB_ZCL_POWER_PROFILE_SCHEDULE_MODE, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_power_profile_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = server_cmd,
	.check = check,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_power_profile_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = client_cmd,
};
