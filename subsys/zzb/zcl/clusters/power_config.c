/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zzb/zcl/power_config.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.3.1.1) */
#define REVISION 2

/* An unknown MainsVoltage, or a mains voltage threshold without alarm (ZCL 3.3.2.2.2) */
#define NO_VOLTAGE 0xffff

/* Mains voltage alarm codes, also the bits of MainsAlarmMask (ZCL Table 3-20) */
#define MAINS_LOW  0x00
#define MAINS_HIGH 0x01

/* The mains and the first battery source */
static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_power_config, mains_voltage),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_power_config, mains_frequency),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK, ZZB_ZCL_MAP8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config, mains_alarm_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     mains_voltage_min_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     mains_voltage_max_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     mains_voltage_dwell_trip_point),
	/* Reportable for ZHA, although ZCL Table 3-21 does not mark it */
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE, ZZB_ZCL_UINT8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_power_config, battery_voltage),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING, ZZB_ZCL_UINT8,
		     ZZB_ZCL_REPORT, struct zzb_zcl_power_config, battery_percentage_remaining),
	/* The battery the device is built with; ZCL makes these five writable */
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_MANUFACTURER, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_power_config, battery_manufacturer),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_power_config, battery_size),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_AHR_RATING, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_power_config, battery_ahr_rating),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_QUANTITY, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_power_config, battery_quantity),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_RATED_VOLTAGE, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_power_config, battery_rated_voltage),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK, ZZB_ZCL_MAP8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config, battery_alarm_mask),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_voltage_min_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_voltage_threshold1),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD2, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_voltage_threshold2),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD3, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_voltage_threshold3),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_MIN_THRESHOLD, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_percentage_min_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD1, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_percentage_threshold1),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD2, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_percentage_threshold2),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_power_config,
		     battery_percentage_threshold3),
	ZZB_ZCL_ATTR(ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE, ZZB_ZCL_MAP32, ZZB_ZCL_REPORT,
		     struct zzb_zcl_power_config, battery_alarm_state),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The values of the Power Configuration server on ep, or NULL */
static struct zzb_zcl_power_config *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_POWER_CONFIG, true);

	return cl != NULL ? cl->values : NULL;
}

/* The mains voltage alarms whose conditions hold, as bits of their codes */
static uint8_t mains_conditions(const struct zzb_zcl_power_config *v)
{
	uint8_t conditions = 0;

	if (v->mains_voltage == NO_VOLTAGE) {
		return 0;
	}

	if (v->mains_voltage_min_threshold != NO_VOLTAGE &&
	    v->mains_voltage < v->mains_voltage_min_threshold) {
		conditions |= BIT(MAINS_LOW);
	}

	if (v->mains_voltage_max_threshold != NO_VOLTAGE &&
	    v->mains_voltage > v->mains_voltage_max_threshold) {
		conditions |= BIT(MAINS_HIGH);
	}

	return conditions;
}

/*
 * k_uptime_get() at which mains voltage alarm code is due, or INT64_MAX when it does not wait for
 * its dwell. The dwell never disables the alarm (CCB 2899).
 */
static int64_t mains_due(const struct zzb_zcl_power_config *v, int code)
{
	if (!(v->mains_active & ~v->mains_raised & BIT(code))) {
		return INT64_MAX;
	}

	return v->mains_since[code] + v->mains_voltage_dwell_trip_point * MSEC_PER_SEC;
}

/* Follows the conditions of the mains voltage alarms and raises those whose dwell has passed */
static void mains_update(struct zzb_node *node, uint8_t ep, struct zzb_zcl_power_config *v,
			 bool notify)
{
	uint8_t conditions = mains_conditions(v);
	uint8_t started = conditions & ~v->mains_active;
	int64_t now = k_uptime_get();

	v->mains_active = conditions;
	v->mains_raised &= conditions;
	ARRAY_FOR_EACH(v->mains_since, code) {
		if (started & BIT(code)) {
			v->mains_since[code] = now;
		}

		if (mains_due(v, code) <= now) {
			v->mains_raised |= BIT(code);
			zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_POWER_CONFIG, code,
					    v->mains_alarm_mask & BIT(code), notify);
		}
	}
}

/* Arms the timer for the earliest mains voltage alarm of the node, or stops it */
static void mains_schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_power_config *v = server(&node->device->endpoints[i]);

		if (v != NULL) {
			next = MIN(next, MIN(mains_due(v, MAINS_LOW), mains_due(v, MAINS_HIGH)));
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.mains_alarm_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.mains_alarm_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

static void mains_tick(struct k_work *work)
{
	struct zzb_node *node = CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node,
					     zcl.mains_alarm_timer);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_power_config *v = server(ep);

		if (v != NULL) {
			mains_update(node, ep->id, v, true);
		}
	}

	mains_schedule(node);
}

/* A measurement that only the application sets; the alarms it raises come without zcl_cb */
static bool measurement(uint16_t attr)
{
	return attr == ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE;
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	mains_update(node, ep, values, !measurement(attr));
	mains_schedule(node);
}

/* A condition at boot starts its dwell now */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	mains_update(node, ep, values, false);
	mains_schedule(node);
}

/* Raises again the alarms of code whose conditions last, all of them for 0xff (ZCL 3.11.2.4.1) */
static void alarm_reset(struct zzb_node *node, uint8_t ep, void *values, uint8_t code)
{
	const struct zzb_zcl_power_config *v = values;

	ARRAY_FOR_EACH(v->mains_since, c) {
		if ((code == c || code == 0xff) && (v->mains_raised & BIT(c))) {
			zzb_zcl_alarm_raise(node, ep, ZZB_ZCL_POWER_CONFIG, c,
					    v->mains_alarm_mask & BIT(c), true);
		}
	}
}

/* The defaults of ZCL 3.3.2.2; the fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_power_config *v = values;

	v->mains_alarm_mask = 0;
	v->mains_voltage_min_threshold = 0;
	v->mains_voltage_max_threshold = 0xffff;
	v->mains_voltage_dwell_trip_point = 0;
	v->battery_alarm_mask = 0;
	v->battery_voltage_min_threshold = 0;
	v->battery_voltage_threshold1 = 0;
	v->battery_voltage_threshold2 = 0;
	v->battery_voltage_threshold3 = 0;
	v->battery_percentage_min_threshold = 0;
	v->battery_percentage_threshold1 = 0;
	v->battery_percentage_threshold2 = 0;
	v->battery_percentage_threshold3 = 0;
	v->battery_alarm_state = 0;
	v->mains_active = 0;
	v->mains_raised = 0;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_POWER_CONFIG, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD, 0xffff);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD2, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD3, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_MIN_THRESHOLD, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD1, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD2, 0);
	set(node, ep, ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3, 0);
}

const struct zzb_zcl_cluster_def zzb_zcl_power_config_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.changed = changed,
	.alarm_reset = alarm_reset,
};

void zzb_zcl_power_config_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.mains_alarm_timer, mains_tick);
}
