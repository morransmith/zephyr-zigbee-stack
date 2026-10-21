/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zzb/zcl/device_temp.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.4.1.1) */
#define REVISION 1

/* The non-value of int16 (ZCL Table 2-11) as a value of the stack */
#define NON_VALUE 0x8000

/* Alarm codes, also the bits of DeviceTempAlarmMask (ZCL Table 3-30) */
#define TOO_LOW  0x00
#define TOO_HIGH 0x01

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

/* The values of the Device Temperature Configuration server on ep, or NULL */
static struct zzb_zcl_device_temp *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_DEVICE_TEMP, true);

	return cl != NULL ? cl->values : NULL;
}

/* The alarms whose conditions hold, as bits of their codes */
static uint8_t holding(const struct zzb_zcl_device_temp *v)
{
	int16_t t = v->current_temperature;
	uint8_t conditions = 0;

	if (t == INT16_MIN) {
		return 0;
	}

	if (v->low_temp_threshold != INT16_MIN &&
	    v->low_temp_dwell_trip_point != ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE &&
	    t < v->low_temp_threshold) {
		conditions |= BIT(TOO_LOW);
	}

	if (v->high_temp_threshold != INT16_MIN &&
	    v->high_temp_dwell_trip_point != ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE &&
	    t > v->high_temp_threshold) {
		conditions |= BIT(TOO_HIGH);
	}

	return conditions;
}

/* k_uptime_get() at which alarm code is due, or INT64_MAX when it does not wait for its dwell */
static int64_t due(const struct zzb_zcl_device_temp *v, int code)
{
	uint32_t dwell =
		code == TOO_LOW ? v->low_temp_dwell_trip_point : v->high_temp_dwell_trip_point;

	if (!(v->alarm_active & ~v->alarm_raised & BIT(code))) {
		return INT64_MAX;
	}

	return v->alarm_since[code] + (int64_t)dwell * MSEC_PER_SEC;
}

/* Follows the conditions of the alarms and raises those whose dwell has passed */
static void update(struct zzb_node *node, uint8_t ep, struct zzb_zcl_device_temp *v, bool notify)
{
	uint8_t conditions = holding(v);
	uint8_t started = conditions & ~v->alarm_active;
	int64_t now = k_uptime_get();

	v->alarm_active = conditions;
	v->alarm_raised &= conditions;
	ARRAY_FOR_EACH(v->alarm_since, code) {
		if (started & BIT(code)) {
			v->alarm_since[code] = now;
		}

		if (due(v, code) <= now) {
			v->alarm_raised |= BIT(code);
			zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_DEVICE_TEMP, code,
					    v->alarm_mask & BIT(code), notify);
		}
	}
}

/* Arms the timer for the earliest alarm of the node, or stops it */
static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_device_temp *v = server(&node->device->endpoints[i]);

		if (v != NULL) {
			next = MIN(next, MIN(due(v, TOO_LOW), due(v, TOO_HIGH)));
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.temp_alarm_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.temp_alarm_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

static void tick(struct k_work *work)
{
	struct zzb_node *node = CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node,
					     zcl.temp_alarm_timer);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_device_temp *v = server(ep);

		if (v != NULL) {
			update(node, ep->id, v, true);
		}
	}

	schedule(node);
}

/*
 * CurrentTemperature is set only by the application, so the alarms it raises come without zcl_cb.
 * The alarms are updated before track(), whose writes come back here.
 */
static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	bool measured = attr == ZZB_ZCL_DEVICE_TEMP_CURRENT_TEMPERATURE;

	update(node, ep, values, !measured);
	schedule(node);
	if (measured) {
		track(node, ep, values);
	}
}

/* The temperature that the application set before zzb_node_init(); a condition starts its dwell */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	update(node, ep, values, false);
	schedule(node);
	track(node, ep, values);
}

/* Raises again the alarms of code whose conditions last, all of them for 0xff (ZCL 3.11.2.4.1) */
static void alarm_reset(struct zzb_node *node, uint8_t ep, void *values, uint8_t code)
{
	const struct zzb_zcl_device_temp *v = values;

	ARRAY_FOR_EACH(v->alarm_since, c) {
		if ((code == c || code == 0xff) && (v->alarm_raised & BIT(c))) {
			zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_DEVICE_TEMP, c,
					    v->alarm_mask & BIT(c), true);
		}
	}
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
	v->low_temp_dwell_trip_point = ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE;
	v->high_temp_dwell_trip_point = ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE;
	v->alarm_active = 0;
	v->alarm_raised = 0;
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
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_LOW_TEMP_DWELL_TRIP_POINT,
	    ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE);
	set(node, ep, ZZB_ZCL_DEVICE_TEMP_HIGH_TEMP_DWELL_TRIP_POINT,
	    ZZB_ZCL_DEVICE_TEMP_DWELL_TRIP_POINT_NONE);
}

const struct zzb_zcl_cluster_def zzb_zcl_device_temp_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.changed = changed,
	.alarm_reset = alarm_reset,
};

void zzb_zcl_device_temp_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.temp_alarm_timer, tick);
}
