/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/ias_wd.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 8.4.1.1) */
#define REVISION 2

/* MaxDuration (ZCL Table 8-19) */
#define MAX_DURATION_DEFAULT 240
#define MAX_DURATION_INVALID 0xffff

/* The octet of the fields, warning duration, strobe duty cycle and strobe level */
#define START_WARNING_LEN 5

#define WARNING_MODE_MAX 6
#define STROBE_LEVEL_MAX 3
#define DUTY_CYCLE_MAX   100

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_WD_MAX_DURATION, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ias_wd, max_duration),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 8.4.2.2.1.6: the nearest multiple of 10, 5 upwards */
static uint8_t duty_cycle(uint8_t value)
{
	return MIN((value + 5) / 10 * 10, DUTY_CYCLE_MAX);
}

/* ZCL 8.4.2.2.1 */
static int start_warning(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 const struct zzb_zcl_ias_wd *v)
{
	/* A copy of the whole request, which zzb_zcl_cmd_respond() can answer */
	struct zzb_zcl_req req = *CONTAINER_OF(cmd, struct zzb_zcl_req, cmd);
	uint8_t payload[START_WARNING_LEN];

	if (cmd->len < START_WARNING_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (FIELD_GET(ZZB_ZCL_IAS_WD_WARNING_MODE, cmd->payload[0]) > WARNING_MODE_MAX ||
	    FIELD_GET(ZZB_ZCL_IAS_WD_STROBE, cmd->payload[0]) > 1 ||
	    cmd->payload[4] > STROBE_LEVEL_MAX) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	memcpy(payload, cmd->payload, sizeof(payload));
	sys_put_le16(MIN(sys_get_le16(&payload[1]), v->max_duration), &payload[1]);
	payload[3] = duty_cycle(payload[3]);
	req.cmd.payload = payload;
	req.cmd.len = sizeof(payload);
	return zzb_zcl_app_cmd(node, &req.cmd);
}

/* ZCL 8.4.2.2.2 */
static int squawk(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (FIELD_GET(ZZB_ZCL_IAS_WD_SQUAWK_MODE, cmd->payload[0]) > 1) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	return zzb_zcl_app_cmd(node, cmd);
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_IAS_WD_START_WARNING:
		return start_warning(node, cmd, values);
	case ZZB_ZCL_IAS_WD_SQUAWK:
		return squawk(node, cmd);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* MaxDuration is the only writable attribute */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	return value == MAX_DURATION_INVALID ? ZZB_ZCL_INVALID_VALUE : ZZB_ZCL_SUCCESS;
}

static void init(void *values)
{
	struct zzb_zcl_ias_wd *v = values;

	v->max_duration = MAX_DURATION_DEFAULT;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_IAS_WD, ZZB_ZCL_IAS_WD_MAX_DURATION, MAX_DURATION_DEFAULT);
}

const struct zzb_zcl_cluster_def zzb_zcl_ias_wd_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
};
