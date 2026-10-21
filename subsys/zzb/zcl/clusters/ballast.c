/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/ballast.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 5.3.1.1) */
#define REVISION 3

/* The lowest BallastFactorAdjustment (ZCL Table 5.32) */
#define FACTOR_ADJUSTMENT_MIN 0x64

/* Alarm code of LampBurnHoursTripPoint, sent with bit 0 of LampAlarmMode (ZCL 5.3.2.2.4.6) */
#define BURN_HOURS_ALARM 0x01

/* PowerOnLevel and PowerOnFadeTime are deprecated, LampType and LampManufacturer not implemented */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_PHYSICAL_MIN_LEVEL, ZZB_ZCL_UINT8, 0, struct zzb_zcl_ballast,
		     physical_min_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_PHYSICAL_MAX_LEVEL, ZZB_ZCL_UINT8, 0, struct zzb_zcl_ballast,
		     physical_max_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_STATUS, ZZB_ZCL_MAP8, 0, struct zzb_zcl_ballast,
		     ballast_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_MIN_LEVEL, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, min_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_MAX_LEVEL, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, max_level),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_INTRINSIC_FACTOR, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, intrinsic_ballast_factor),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, ballast_factor_adjustment),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_LAMP_QUANTITY, ZZB_ZCL_UINT8, 0, struct zzb_zcl_ballast,
		     lamp_quantity),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_LAMP_RATED_HOURS, ZZB_ZCL_UINT24, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, lamp_rated_hours),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, ZZB_ZCL_UINT24, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, lamp_burn_hours),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_LAMP_ALARM_MODE, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ballast, lamp_alarm_mode),
	ZZB_ZCL_ATTR(ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT, ZZB_ZCL_UINT24,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_ballast,
		     lamp_burn_hours_trip_point),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static uint8_t valid(bool cond)
{
	return cond ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
}

/* ZCL 5.3.2.2.2 */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	const struct zzb_zcl_ballast *v = values;

	switch (attr->id) {
	case ZZB_ZCL_BALLAST_MIN_LEVEL:
		return valid(value >= v->physical_min_level && value <= v->max_level);
	case ZZB_ZCL_BALLAST_MAX_LEVEL:
		return valid(value >= v->min_level && value <= v->physical_max_level);
	case ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT:
		return valid(value >= FACTOR_ADJUSTMENT_MIN);
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

/* Follows the condition of the lamp burn hours alarm and raises the alarm when it starts */
static void update(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ballast *v, bool notify)
{
	bool holds =
		v->lamp_burn_hours_trip_point != ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT_OFF &&
		v->lamp_burn_hours != ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_UNKNOWN &&
		v->lamp_burn_hours >= v->lamp_burn_hours_trip_point;
	bool started = holds && !v->burn_hours_alarm;

	v->burn_hours_alarm = holds;
	if (started) {
		zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_BALLAST, BURN_HOURS_ALARM,
				    v->lamp_alarm_mode & ZZB_ZCL_BALLAST_LAMP_ALARM_MODE_BURN_HOURS,
				    notify);
	}
}

/* The application counts LampBurnHours, so the alarm it raises comes without zcl_cb */
static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	update(node, ep, values, attr != ZZB_ZCL_BALLAST_LAMP_BURN_HOURS);
}

static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	update(node, ep, values, false);
}

/* Raises the alarm again while its condition lasts (ZCL 3.11.2.4.1) */
static void alarm_reset(struct zzb_node *node, uint8_t ep, void *values, uint8_t code)
{
	const struct zzb_zcl_ballast *v = values;

	if ((code == BURN_HOURS_ALARM || code == 0xff) && v->burn_hours_alarm) {
		zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_BALLAST, BURN_HOURS_ALARM,
				    v->lamp_alarm_mode & ZZB_ZCL_BALLAST_LAMP_ALARM_MODE_BURN_HOURS,
				    true);
	}
}

/* The defaults of ZCL 5.3.2.2; IntrinsicBallastFactor has none, so it is unknown */
static void init(void *values)
{
	struct zzb_zcl_ballast *v = values;

	v->min_level = v->physical_min_level;
	v->max_level = v->physical_max_level;
	v->intrinsic_ballast_factor = ZZB_ZCL_BALLAST_INTRINSIC_FACTOR_UNKNOWN;
	v->ballast_factor_adjustment = ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT_OFF;
	v->lamp_rated_hours = ZZB_ZCL_BALLAST_LAMP_RATED_HOURS_UNKNOWN;
	v->lamp_burn_hours = 0;
	v->lamp_alarm_mode = 0;
	v->lamp_burn_hours_trip_point = ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT_OFF;
	v->burn_hours_alarm = false;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_BALLAST, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_zcl_ballast *v = values;

	set(node, ep, ZZB_ZCL_BALLAST_MIN_LEVEL, v->physical_min_level);
	set(node, ep, ZZB_ZCL_BALLAST_MAX_LEVEL, v->physical_max_level);
	set(node, ep, ZZB_ZCL_BALLAST_INTRINSIC_FACTOR, ZZB_ZCL_BALLAST_INTRINSIC_FACTOR_UNKNOWN);
	set(node, ep, ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT, ZZB_ZCL_BALLAST_FACTOR_ADJUSTMENT_OFF);
	set(node, ep, ZZB_ZCL_BALLAST_LAMP_RATED_HOURS, ZZB_ZCL_BALLAST_LAMP_RATED_HOURS_UNKNOWN);
	set(node, ep, ZZB_ZCL_BALLAST_LAMP_BURN_HOURS, 0);
	set(node, ep, ZZB_ZCL_BALLAST_LAMP_ALARM_MODE, 0);
	set(node, ep, ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT,
	    ZZB_ZCL_BALLAST_LAMP_BURN_HOURS_TRIP_POINT_OFF);
}

const struct zzb_zcl_cluster_def zzb_zcl_ballast_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.check = check,
	.changed = changed,
	.alarm_reset = alarm_reset,
};
