/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zzb/zcl/thermostat.h>

#include "common/node.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* Number of Transitions, Day of Week and Mode for Sequence (ZCL 6.3.2.3.2) */
#define HDR_LEN 3

/* The longest transition: time, heating and cooling setpoint */
#define TRANSITION_LEN 6

#define MODE_MASK (ZZB_ZCL_SCHEDULE_HEAT | ZZB_ZCL_SCHEDULE_COOL)

/* 23:59 in minutes since midnight */
#define LAST_MINUTE 1439

#define RECORD_VERSION 1
/* Version, mode and number of transitions */
#define RECORD_HDR_LEN 3
/* Room for zcl/sched/<ep>/<day> */
#define NAME_LEN       16

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

static void record_name(char name[NAME_LEN], uint8_t ep, int day)
{
	snprintk(name, NAME_LEN, "zcl/sched/%u/%d", ep, day);
}

/* Saves day i of the schedule on ep, or deletes the record of an empty day; errors are ignored */
static void save(struct zzb_node *node, uint8_t ep, int i, const struct zzb_zcl_schedule_day *day)
{
	uint8_t rec[RECORD_HDR_LEN + TRANSITION_LEN * CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS] = {
		RECORD_VERSION, day->mode, day->count};
	uint8_t *p = rec + RECORD_HDR_LEN;
	char name[NAME_LEN];

	record_name(name, ep, i);
	if (day->count == 0) {
		zzb_settings_delete(node, name);
		return;
	}

	for (int j = 0; j < day->count; j++, p += TRANSITION_LEN) {
		sys_put_le16(day->transitions[j].time, p);
		sys_put_le16(day->transitions[j].heating, p + 2);
		sys_put_le16(day->transitions[j].cooling, p + 4);
	}

	zzb_settings_save(node, name, rec, p - rec);
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
			save(node, cmd->ep, i, &day);
		}
	}

	return ZZB_ZCL_SUCCESS;
}

/* Writes t at *p, which it passes, when present */
static void put_setpoint(uint8_t **p, int16_t t, bool present)
{
	if (present) {
		sys_put_le16(t, *p);
		*p += 2;
	}
}

/*
 * Writes the Get Weekly Schedule Response (ZCL 6.3.2.4.1) of day, bit bit of Day of Week, with the
 * setpoints of mode to rsp; returns its length
 */
static size_t encode(uint8_t *rsp, const struct zzb_zcl_schedule_day *day, uint8_t bit,
		     uint8_t mode)
{
	uint8_t *p = rsp + HDR_LEN;

	rsp[0] = day->count;
	rsp[1] = bit;
	rsp[2] = mode;
	for (int i = 0; i < day->count; i++) {
		const struct zzb_zcl_schedule_transition *t = &day->transitions[i];

		sys_put_le16(t->time, p);
		p += 2;
		put_setpoint(&p, t->heating, mode & ZZB_ZCL_SCHEDULE_HEAT);
		put_setpoint(&p, t->cooling, mode & ZZB_ZCL_SCHEDULE_COOL);
	}

	return p - rsp;
}

int zzb_zcl_schedule_get(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 const struct zzb_zcl_thermostat *v)
{
	uint8_t rsp[HDR_LEN + TRANSITION_LEN * CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS];
	bool found = false;
	uint8_t days;
	uint8_t mode;

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	if (!cmd->unicast) {
		return ZZB_ZCL_SUCCESS;
	}

	days = cmd->payload[0];
	mode = cmd->payload[1];
	for (int i = 0; i < ARRAY_SIZE(v->schedule); i++) {
		const struct zzb_zcl_schedule_day *day = &v->schedule[i];

		if (!(days & BIT(i)) || !(day->mode & mode)) {
			continue;
		}

		zzb_zcl_cmd_reply(node, cmd, ZZB_ZCL_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP, rsp,
				  encode(rsp, day, BIT(i), day->mode & mode));
		found = true;
	}

	/* No transitions, with the requested days and mode */
	if (!found) {
		const uint8_t none[] = {0, days, mode};

		zzb_zcl_cmd_reply(node, cmd, ZZB_ZCL_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP, none,
				  sizeof(none));
	}

	return ZZB_ZCL_RESPONDED;
}

void zzb_zcl_schedule_clear(struct zzb_node *node, uint8_t ep, struct zzb_zcl_thermostat *v)
{
	char name[NAME_LEN];

	memset(v->schedule, 0, sizeof(v->schedule));
	for (int i = 0; i < ARRAY_SIZE(v->schedule); i++) {
		record_name(name, ep, i);
		zzb_settings_delete(node, name);
	}
}

/* The values of the Thermostat server on ep, or NULL */
static struct zzb_zcl_thermostat *server(struct zzb_node *node, unsigned long ep)
{
	const struct zzb_endpoint *e = ep <= UINT8_MAX ? zzb_node_endpoint(node, ep) : NULL;
	const struct zzb_zcl_cluster *cl;

	if (e == NULL) {
		return NULL;
	}

	cl = zzb_zcl_find_cluster(e, ZZB_ZCL_THERMOSTAT, true);
	return cl != NULL ? cl->values : NULL;
}

int zzb_zcl_schedule_load(struct zzb_node *node, const char *key, size_t len,
			  settings_read_cb read_cb, void *cb_arg)
{
	uint8_t rec[RECORD_HDR_LEN + TRANSITION_LEN * CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS];
	struct zzb_zcl_schedule_day *day;
	struct zzb_zcl_thermostat *v;
	unsigned long ep, i;
	char *end;
	int err;

	/* <ep>/<day> */
	ep = strtoul(key, &end, 10);
	i = *end == '/' ? strtoul(end + 1, &end, 10) : ULONG_MAX;
	v = server(node, ep);
	if (v == NULL || i >= ARRAY_SIZE(v->schedule)) {
		return 0;
	}

	if (len < RECORD_HDR_LEN || len > sizeof(rec)) {
		return -EINVAL;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, len);
	if (err) {
		return err;
	}

	if (len != RECORD_HDR_LEN + TRANSITION_LEN * rec[2]) {
		return -EINVAL;
	}

	day = &v->schedule[i];
	day->mode = rec[1];
	day->count = rec[2];
	for (int j = 0; j < day->count; j++) {
		const uint8_t *p = rec + RECORD_HDR_LEN + TRANSITION_LEN * j;

		day->transitions[j] = (struct zzb_zcl_schedule_transition){
			.time = sys_get_le16(p),
			.heating = sys_get_le16(p + 2),
			.cooling = sys_get_le16(p + 4),
		};
	}

	return 0;
}
