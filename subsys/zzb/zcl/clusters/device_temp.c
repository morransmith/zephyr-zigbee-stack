/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/device_temp.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.4.1.1) */
#define REVISION 1

/* The non-value of int16 (ZCL Table 2-11) as a value of the stack */
#define NON_VALUE 0x8000

static const struct zzb_zcl_attr attrs[] = {
	/* Reportable for ZHA, although ZCL Table 3-28 does not mark it */
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_device_temp, current_temperature),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_MIN_TEMP_EXPERIENCED, ZZB_ZCL_INT16, ZZB_ZCL_NV,
		     struct zzb_zcl_device_temp, min_temp_experienced),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_MAX_TEMP_EXPERIENCED, ZZB_ZCL_INT16, ZZB_ZCL_NV,
		     struct zzb_zcl_device_temp, max_temp_experienced),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_OVER_TEMP_TOTAL_DWELL, ZZB_ZCL_UINT16, ZZB_ZCL_NV,
		     struct zzb_zcl_device_temp, over_temp_total_dwell),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_ALARM_MASK, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_device_temp, alarm_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_THRESHOLD, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_device_temp, low_temp_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_THRESHOLD, ZZB_ZCL_INT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_device_temp, high_temp_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_DWELL_TRIP_POINT, ZZB_ZCL_UINT24,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_device_temp,
		     low_temp_dwell_trip_point),
	ZZB_ZCL_ATTR(ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_DWELL_TRIP_POINT, ZZB_ZCL_UINT24,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_device_temp,
		     high_temp_dwell_trip_point),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* MinTempExperienced and MaxTempExperienced take a known temperature beyond them, without zcl_cb */
static void track(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_device_temp *v)
{
	int16_t t = v->current_temperature;

	if (t == INT16_MIN) {
		return;
	}

	if (v->min_temp_experienced == INT16_MIN || t < v->min_temp_experienced) {
		zzb_zcl_store(node, ep, ZZB_ZCL_DEVICE_TEMP,
			      ZZB_ZCL_DEVICE_TEMP_MIN_TEMP_EXPERIENCED, (uint16_t)t);
	}

	if (v->max_temp_experienced == INT16_MIN || t > v->max_temp_experienced) {
		zzb_zcl_store(node, ep, ZZB_ZCL_DEVICE_TEMP,
			      ZZB_ZCL_DEVICE_TEMP_MAX_TEMP_EXPERIENCED, (uint16_t)t);
	}
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	if (attr == ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE) {
		track(node, ep, values);
	}
}

/* The temperature that the application set before zzb_node_init() */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	track(node, ep, values);
}

/* The defaults of ZCL 3.4.2.2; the fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_device_temp *v = values;

	v->min_temp_experienced = INT16_MIN;
	v->max_temp_experienced = INT16_MIN;
	v->alarm_mask = 0;
	v->low_temp_threshold = INT16_MIN;
	v->high_temp_threshold = INT16_MIN;
	v->low_temp_dwell_trip_point = 0xffffff;
	v->high_temp_dwell_trip_point = 0xffffff;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_DEVICE_TEMP, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_MIN_TEMP_EXPERIENCED, NON_VALUE);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_MAX_TEMP_EXPERIENCED, NON_VALUE);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_ALARM_MASK, 0);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_THRESHOLD, NON_VALUE);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_THRESHOLD, NON_VALUE);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_DWELL_TRIP_POINT, 0xffffff);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_DWELL_TRIP_POINT, 0xffffff);
}

const struct zzb_zcl_cluster_def zzb_zcl_device_temp_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.changed = changed,
};
