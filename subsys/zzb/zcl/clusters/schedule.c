/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/thermostat.h>

#include "zcl/zcl.h"

/* Number of Transitions, Day of Week and Mode for Sequence (ZCL 6.3.2.3.2) */
#define HDR_LEN 3

#define MODE_MASK (ZZB_ZCL_SCHEDULE_HEAT | ZZB_ZCL_SCHEDULE_COOL)

/* 23:59 in minutes since midnight */
#define LAST_MINUTE 1439

/* The length of a transition with the setpoints of mode */
static size_t transition_len(uint8_t mode)
{
	return 2 + ((mode & ZZB_ZCL_SCHEDULE_HEAT) ? 2 : 0) +
	       ((mode & ZZB_ZCL_SCHEDULE_COOL) ? 2 : 0);
}

static bool valid(const struct zzb_zcl_thermostat *v, uint8_t mode,
		  const struct zzb_zcl_schedule_transition *t)
{
	if (t->time > LAST_MINUTE) {
		return false;
	}

	if ((mode & ZZB_ZCL_SCHEDULE_HEAT) &&
	    !IN_RANGE(t->heating, v->abs_min_heat_setpoint_limit, v->abs_max_heat_setpoint_limit)) {
		return false;
	}

	return !(mode & ZZB_ZCL_SCHEDULE_COOL) ||
	       IN_RANGE(t->cooling, v->abs_min_cool_setpoint_limit, v->abs_max_cool_setpoint_limit);
}

/* The setpoint at *p, which it passes, when present; INT16_MIN otherwise */
static int16_t setpoint(const uint8_t **p, bool present)
{
	int16_t t;

	if (!present) {
		return INT16_MIN;
	}

	t = sys_get_le16(*p);
	*p += 2;
	return t;
}

/* Reads the transitions of day at p and checks them (ZCL 6.3.2.3.2.8) */
static uint8_t parse(const struct zzb_zcl_thermostat *v, const uint8_t *p,
		     struct zzb_zcl_schedule_day *day)
{
	for (int i = 0; i < day->count; i++) {
		struct zzb_zcl_schedule_transition *t = &day->transitions[i];

		t->time = sys_get_le16(p);
		p += 2;
		t->heating = setpoint(&p, day->mode & ZZB_ZCL_SCHEDULE_HEAT);
		t->cooling = setpoint(&p, day->mode & ZZB_ZCL_SCHEDULE_COOL);
		if (!valid(v, day->mode, t)) {
			return ZZB_ZCL_INVALID_VALUE;
		}
	}

	/* Overlapping transitions */
	for (int i = 0; i < day->count; i++) {
		for (int j = 0; j < i; j++) {
			if (day->transitions[i].time == day->transitions[j].time) {
				return ZZB_ZCL_FAILURE;
			}
		}
	}

	return ZZB_ZCL_SUCCESS;
}

int zzb_zcl_schedule_set(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_thermostat *v)
{
	struct zzb_zcl_schedule_day day = {0};
	uint8_t days;
	uint8_t status;

	if (cmd->len < HDR_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	day.count = cmd->payload[0];
	days = cmd->payload[1];
	day.mode = cmd->payload[2] & MODE_MASK;
	if (day.mode == 0 || days == 0) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	if (day.count > CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS) {
		return ZZB_ZCL_INSUFFICIENT_SPACE;
	}

	if (cmd->len < HDR_LEN + day.count * transition_len(day.mode)) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	status = parse(v, cmd->payload + HDR_LEN, &day);
	if (status != ZZB_ZCL_SUCCESS) {
		return status;
	}

	if (day.count == 0) {
		day.mode = 0;
	}

	for (int i = 0; i < ARRAY_SIZE(v->schedule); i++) {
		if (days & BIT(i)) {
			v->schedule[i] = day;
		}
	}

	return ZZB_ZCL_SUCCESS;
}
