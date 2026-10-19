/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/power_config.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.3.1.1) */
#define REVISION 2

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
	.reset = reset,
};
