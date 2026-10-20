/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/measurement.h>

#include "zcl/zcl.h"

/* ClusterRevision of Temperature Measurement (ZCL 4.4.1.1) */
#define TEMPERATURE_REVISION 3

/* ClusterRevision of Pressure Measurement (ZCL 4.5.1.1) */
#define PRESSURE_REVISION 2

/* ClusterRevision of Flow Measurement (ZCL 4.6.1.1) */
#define FLOW_REVISION 2

/* ClusterRevision of Relative Humidity Measurement (ZCL 4.7.1.1) */
#define HUMIDITY_REVISION 2

static const struct zzb_zcl_attr temperature_attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_temperature, measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MIN_MEASURED_VALUE, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_temperature, min_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MAX_MEASURED_VALUE, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_temperature, max_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_temperature,
		     tolerance),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, TEMPERATURE_REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_temperature_def = {
	.attrs = temperature_attrs,
	.attr_count = ARRAY_SIZE(temperature_attrs),
};

static const struct zzb_zcl_attr pressure_attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_pressure, measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MIN_MEASURED_VALUE, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_pressure, min_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MAX_MEASURED_VALUE, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_pressure, max_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pressure,
		     tolerance),
	ZZB_ZCL_ATTR(ZZB_ZCL_PRESSURE_SCALED_VALUE, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_pressure, scaled_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_PRESSURE_MIN_SCALED_VALUE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pressure,
		     min_scaled_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_PRESSURE_MAX_SCALED_VALUE, ZZB_ZCL_INT16, 0, struct zzb_zcl_pressure,
		     max_scaled_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_PRESSURE_SCALED_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_pressure,
		     scaled_tolerance),
	ZZB_ZCL_ATTR(ZZB_ZCL_PRESSURE_SCALE, ZZB_ZCL_INT8, 0, struct zzb_zcl_pressure, scale),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, PRESSURE_REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_pressure_def = {
	.attrs = pressure_attrs,
	.attr_count = ARRAY_SIZE(pressure_attrs),
};

static const struct zzb_zcl_attr flow_attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_flow, measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MIN_MEASURED_VALUE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_flow,
		     min_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MAX_MEASURED_VALUE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_flow,
		     max_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_flow,
		     tolerance),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, FLOW_REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_flow_def = {
	.attrs = flow_attrs,
	.attr_count = ARRAY_SIZE(flow_attrs),
};

static const struct zzb_zcl_attr humidity_attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MEASURED_VALUE, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_humidity, measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MIN_MEASURED_VALUE, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_humidity, min_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_MAX_MEASURED_VALUE, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_humidity, max_measured_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_MEASUREMENT_TOLERANCE, ZZB_ZCL_UINT16, 0, struct zzb_zcl_humidity,
		     tolerance),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, HUMIDITY_REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_humidity_def = {
	.attrs = humidity_attrs,
	.attr_count = ARRAY_SIZE(humidity_attrs),
};
