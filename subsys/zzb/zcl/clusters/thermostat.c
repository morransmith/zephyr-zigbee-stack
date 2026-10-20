/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zzb/zcl/thermostat.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 6.3.1.1) */
#define REVISION 3

/* The range of LocalTemperatureCalibration (ZCL Table 6-13) */
#define CALIBRATION_MIN -25
#define CALIBRATION_MAX 25

/* Mode of Setpoint Raise/Lower (ZCL Table 6-36) */
#define MODE_HEAT 0x00
#define MODE_COOL 0x01
#define MODE_BOTH 0x02

/* The default setpoints of ZCL Table 6-13, 26 and 20 degrees Celsius */
#define DEFAULT_COOLING 2600
#define DEFAULT_HEATING 2000

/*
 * ZHA configures the reports of Occupancy, the setpoints, SystemMode, ThermostatRunningMode and
 * ThermostatRunningState, which ZCL does not mark reportable. HVACSystemTypeConfiguration,
 * RemoteSensing, TemperatureSetpointHold, TemperatureSetpointHoldDuration and the attribute sets
 * from 0x0030 are not implemented.
 */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, local_temperature),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_OUTDOOR_TEMPERATURE, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_thermostat, outdoor_temperature),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_OCCUPANCY, ZZB_ZCL_MAP8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, occupancy),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_ABS_MIN_HEAT_SETPOINT_LIMIT, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_thermostat, abs_min_heat_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_ABS_MAX_HEAT_SETPOINT_LIMIT, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_thermostat, abs_max_heat_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_ABS_MIN_COOL_SETPOINT_LIMIT, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_thermostat, abs_min_cool_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_ABS_MAX_COOL_SETPOINT_LIMIT, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_thermostat, abs_max_cool_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_PI_COOLING_DEMAND, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, pi_cooling_demand),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_PI_HEATING_DEMAND, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, pi_heating_demand),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION, ZZB_ZCL_INT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     local_temperature_calibration),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     occupied_cooling_setpoint),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     occupied_heating_setpoint),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     unoccupied_cooling_setpoint),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     unoccupied_heating_setpoint),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     min_heat_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     max_heat_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     min_cool_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     max_cool_setpoint_limit),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_MIN_SETPOINT_DEAD_BAND, ZZB_ZCL_INT8, 0,
		     struct zzb_zcl_thermostat, min_setpoint_dead_band),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_thermostat, control_sequence_of_operation),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_SYSTEM_MODE, ZZB_ZCL_ENUM8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     system_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_ALARM_MASK, ZZB_ZCL_MAP8, 0, struct zzb_zcl_thermostat,
		     alarm_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_RUNNING_MODE, ZZB_ZCL_ENUM8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, thermostat_running_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_START_OF_WEEK, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_thermostat,
		     start_of_week),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_THERMOSTAT_NUMBER_OF_WEEKLY_TRANSITIONS, ZZB_ZCL_UINT8,
			   7 * CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_THERMOSTAT_NUMBER_OF_DAILY_TRANSITIONS, ZZB_ZCL_UINT8,
			   CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE, ZZB_ZCL_MAP8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV, struct zzb_zcl_thermostat,
		     thermostat_programming_operation_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_THERMOSTAT_RUNNING_STATE, ZZB_ZCL_MAP16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_thermostat, thermostat_running_state),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 6.3.2.6 */
static const struct zzb_zcl_scene_attr scene_attrs[] = {
	{ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, ZZB_ZCL_INT16},
	{ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, ZZB_ZCL_INT16},
	{ZZB_ZCL_THERMOSTAT_SYSTEM_MODE, ZZB_ZCL_ENUM8},
};

static uint8_t valid(bool cond)
{
	return cond ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

/*
 * The dead band applies only to the sequences that both heat and cool, so that a setpoint that is
 * not used does not hold back the other
 */
static bool heats_and_cools(const struct zzb_zcl_thermostat *v)
{
	return v->control_sequence_of_operation == ZZB_ZCL_SEQUENCE_COOLING_HEATING ||
	       v->control_sequence_of_operation == ZZB_ZCL_SEQUENCE_COOLING_HEATING_REHEAT;
}

/* MinSetpointDeadBand in 0.01 degrees Celsius */
static int dead_band(const struct zzb_zcl_thermostat *v)
{
	return 10 * v->min_setpoint_dead_band;
}

/* ZCL 6.3.2.2.2.2: whether t is a valid heating setpoint in the pair with cooling */
static bool heating_valid(const struct zzb_zcl_thermostat *v, int16_t t, int16_t cooling)
{
	if (t < v->min_heat_setpoint_limit || t > v->max_heat_setpoint_limit) {
		return false;
	}

	return !heats_and_cools(v) || t <= cooling - dead_band(v);
}

static bool cooling_valid(const struct zzb_zcl_thermostat *v, int16_t t, int16_t heating)
{
	if (t < v->min_cool_setpoint_limit || t > v->max_cool_setpoint_limit) {
		return false;
	}

	return !heats_and_cools(v) || t >= heating + dead_band(v);
}

/* AbsMin <= min <= the heating setpoints <= max <= AbsMax */
static bool heat_limits(const struct zzb_zcl_thermostat *v, int16_t min, int16_t max)
{
	return v->abs_min_heat_setpoint_limit <= min &&
	       min <= MIN(v->occupied_heating_setpoint, v->unoccupied_heating_setpoint) &&
	       MAX(v->occupied_heating_setpoint, v->unoccupied_heating_setpoint) <= max &&
	       max <= v->abs_max_heat_setpoint_limit;
}

/* AbsMin <= min <= the cooling setpoints <= max <= AbsMax */
static bool cool_limits(const struct zzb_zcl_thermostat *v, int16_t min, int16_t max)
{
	return v->abs_min_cool_setpoint_limit <= min &&
	       min <= MIN(v->occupied_cooling_setpoint, v->unoccupied_cooling_setpoint) &&
	       MAX(v->occupied_cooling_setpoint, v->unoccupied_cooling_setpoint) <= max &&
	       max <= v->abs_max_cool_setpoint_limit;
}

/* ZCL Tables 6-15 and 6-16 */
static bool mode_valid(uint8_t sequence, uint32_t mode)
{
	switch (mode) {
	case ZZB_ZCL_SYSTEM_MODE_HEAT:
	case ZZB_ZCL_SYSTEM_MODE_EMERGENCY_HEATING:
		return sequence != ZZB_ZCL_SEQUENCE_COOLING_ONLY &&
		       sequence != ZZB_ZCL_SEQUENCE_COOLING_REHEAT;
	case ZZB_ZCL_SYSTEM_MODE_COOL:
	case ZZB_ZCL_SYSTEM_MODE_PRECOOLING:
		return sequence != ZZB_ZCL_SEQUENCE_HEATING_ONLY &&
		       sequence != ZZB_ZCL_SEQUENCE_HEATING_REHEAT;
	/* 0x02 and the values above Sleep are reserved */
	default:
		return mode != 0x02 && mode <= ZZB_ZCL_SYSTEM_MODE_SLEEP;
	}
}

static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	const struct zzb_zcl_thermostat *v = values;
	int16_t t = (int16_t)value;

	switch (attr->id) {
	case ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT:
		return valid(cooling_valid(v, t, v->occupied_heating_setpoint));
	case ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT:
		return valid(heating_valid(v, t, v->occupied_cooling_setpoint));
	case ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT:
		return valid(cooling_valid(v, t, v->unoccupied_heating_setpoint));
	case ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT:
		return valid(heating_valid(v, t, v->unoccupied_cooling_setpoint));
	case ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT:
		return valid(heat_limits(v, t, v->max_heat_setpoint_limit));
	case ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT:
		return valid(heat_limits(v, v->min_heat_setpoint_limit, t));
	case ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT:
		return valid(cool_limits(v, t, v->max_cool_setpoint_limit));
	case ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT:
		return valid(cool_limits(v, v->min_cool_setpoint_limit, t));
	case ZZB_ZCL_THERMOSTAT_SYSTEM_MODE:
		return valid(mode_valid(v->control_sequence_of_operation, value));
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

/* ZCL 6.3.2.2.2.1, CCB 2477: LocalTemperatureCalibration out of range takes the nearer end */
static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	const struct zzb_zcl_thermostat *v = values;
	int8_t calibration =
		CLAMP(v->local_temperature_calibration, CALIBRATION_MIN, CALIBRATION_MAX);

	if (attr == ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION) {
		zzb_zcl_store(node, ep, ZZB_ZCL_THERMOSTAT, attr, (uint8_t)calibration);
	}
}

static int16_t default_cooling(const struct zzb_zcl_thermostat *v)
{
	return CLAMP(DEFAULT_COOLING, v->abs_min_cool_setpoint_limit,
		     v->abs_max_cool_setpoint_limit);
}

static int16_t default_heating(const struct zzb_zcl_thermostat *v)
{
	return CLAMP(DEFAULT_HEATING, v->abs_min_heat_setpoint_limit,
		     v->abs_max_heat_setpoint_limit);
}

/* The fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_thermostat *v = values;

	v->local_temperature_calibration = 0;
	v->occupied_cooling_setpoint = default_cooling(v);
	v->occupied_heating_setpoint = default_heating(v);
	v->unoccupied_cooling_setpoint = default_cooling(v);
	v->unoccupied_heating_setpoint = default_heating(v);
	v->min_heat_setpoint_limit = v->abs_min_heat_setpoint_limit;
	v->max_heat_setpoint_limit = v->abs_max_heat_setpoint_limit;
	v->min_cool_setpoint_limit = v->abs_min_cool_setpoint_limit;
	v->max_cool_setpoint_limit = v->abs_max_cool_setpoint_limit;
	v->system_mode = v->system_mode_default;
	v->thermostat_programming_operation_mode = 0;
	memset(v->schedule, 0, sizeof(v->schedule));
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_THERMOSTAT, attr, value);
}

/* Temperatures pass as the bits of their int16 values */
static void set_temperature(struct zzb_node *node, uint8_t ep, uint16_t attr, int16_t t)
{
	set(node, ep, attr, (uint16_t)t);
}

/*
 * Moves the heating setpoint of the occupied or unoccupied pair by amount within its limits and,
 * while the thermostat heats and cools, below the cooling setpoint by the dead band
 */
static void raise_heating(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_thermostat *v,
			  bool occupied, int amount)
{
	int16_t heating = occupied ? v->occupied_heating_setpoint : v->unoccupied_heating_setpoint;
	int16_t cooling = occupied ? v->occupied_cooling_setpoint : v->unoccupied_cooling_setpoint;
	int t = CLAMP(heating + amount, v->min_heat_setpoint_limit, v->max_heat_setpoint_limit);

	if (heats_and_cools(v)) {
		t = MIN(t, cooling - dead_band(v));
	}

	set_temperature(node, ep,
			occupied ? ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT
				 : ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT,
			t);
}

static void raise_cooling(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_thermostat *v,
			  bool occupied, int amount)
{
	int16_t heating = occupied ? v->occupied_heating_setpoint : v->unoccupied_heating_setpoint;
	int16_t cooling = occupied ? v->occupied_cooling_setpoint : v->unoccupied_cooling_setpoint;
	int t = CLAMP(cooling + amount, v->min_cool_setpoint_limit, v->max_cool_setpoint_limit);

	if (heats_and_cools(v)) {
		t = MAX(t, heating + dead_band(v));
	}

	set_temperature(node, ep,
			occupied ? ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT
				 : ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT,
			t);
}

/* ZCL 6.3.2.3.1; the unoccupied setpoints change while bit 0 of Occupancy is 0 */
static int setpoint_raise_lower(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
				const struct zzb_zcl_thermostat *v)
{
	bool occupied = v->occupancy & BIT(0);
	uint8_t mode;
	int amount;

	if (cmd->len < 2) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	mode = cmd->payload[0];
	amount = 10 * (int8_t)cmd->payload[1];
	if (mode > MODE_BOTH) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	/* A raise moves the cooling setpoint first and a lower the heating one, so both can move */
	if (mode != MODE_HEAT && amount > 0) {
		raise_cooling(node, cmd->ep, v, occupied, amount);
	}

	if (mode != MODE_COOL) {
		raise_heating(node, cmd->ep, v, occupied, amount);
	}

	if (mode != MODE_HEAT && amount <= 0) {
		raise_cooling(node, cmd->ep, v, occupied, amount);
	}

	return ZZB_ZCL_SUCCESS;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	switch (cmd->id) {
	case ZZB_ZCL_THERMOSTAT_SETPOINT_RAISE_LOWER:
		return setpoint_raise_lower(node, cmd, values);
	case ZZB_ZCL_THERMOSTAT_SET_WEEKLY_SCHEDULE:
		return zzb_zcl_schedule_set(node, cmd, values);
	case ZZB_ZCL_THERMOSTAT_GET_WEEKLY_SCHEDULE:
		return zzb_zcl_schedule_get(node, cmd, values);
	case ZZB_ZCL_THERMOSTAT_CLEAR_WEEKLY_SCHEDULE:
		zzb_zcl_schedule_clear(node, cmd->ep, values);
		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* The values are written at once and without the checks; the transition time is ignored */
static void recall(struct zzb_node *node, uint8_t ep, void *values, const uint32_t *set_values,
		   size_t count, uint32_t transition_ds)
{
	for (size_t i = 0; i < count; i++) {
		set(node, ep, scene_attrs[i].id, set_values[i]);
	}
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_zcl_thermostat *v = values;

	set(node, ep, ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION, 0);
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, default_cooling(v));
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, default_heating(v));
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT,
			default_cooling(v));
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT,
			default_heating(v));
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT,
			v->abs_min_heat_setpoint_limit);
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT,
			v->abs_max_heat_setpoint_limit);
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT,
			v->abs_min_cool_setpoint_limit);
	set_temperature(node, ep, ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT,
			v->abs_max_cool_setpoint_limit);
	set(node, ep, ZZB_ZCL_THERMOSTAT_SYSTEM_MODE, v->system_mode_default);
	set(node, ep, ZZB_ZCL_THERMOSTAT_PROGRAMMING_OPERATION_MODE, 0);
	zzb_zcl_schedule_clear(node, ep, values);
}

const struct zzb_zcl_cluster_def zzb_zcl_thermostat_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.scene_attrs = scene_attrs,
	.scene_attr_count = ARRAY_SIZE(scene_attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
	.changed = changed,
	.recall = recall,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_thermostat_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
