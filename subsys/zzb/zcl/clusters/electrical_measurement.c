/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/electrical_measurement.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 4.9.1.1) */
#define REVISION 3

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE, ZZB_ZCL_MAP32, 0,
		     struct zzb_zcl_electrical_measurement, measurement_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, ac_frequency),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MIN, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, ac_frequency_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MAX, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, ac_frequency_max),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_MULTIPLIER, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement,
		     ac_frequency_multiplier),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_DIVISOR, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_frequency_divisor),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, rms_voltage),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_MIN, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, rms_voltage_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_MAX, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, rms_voltage_max),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, rms_current),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_MIN, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, rms_current_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_MAX, ZZB_ZCL_UINT16, 0,
		     struct zzb_zcl_electrical_measurement, rms_current_max),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, active_power),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_MIN, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_electrical_measurement, active_power_min),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_MAX, ZZB_ZCL_INT16, 0,
		     struct zzb_zcl_electrical_measurement, active_power_max),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER, ZZB_ZCL_INT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, reactive_power),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER, ZZB_ZCL_UINT16, ZZB_ZCL_REPORT,
		     struct zzb_zcl_electrical_measurement, apparent_power),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_POWER_FACTOR, ZZB_ZCL_INT8, 0,
		     struct zzb_zcl_electrical_measurement, power_factor),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_MULTIPLIER, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_voltage_multiplier),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_DIVISOR, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_voltage_divisor),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_CURRENT_MULTIPLIER, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_current_multiplier),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_CURRENT_DIVISOR, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_current_divisor),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_POWER_MULTIPLIER, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_power_multiplier),
	ZZB_ZCL_ATTR(ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_POWER_DIVISOR, ZZB_ZCL_UINT16,
		     ZZB_ZCL_REPORT, struct zzb_zcl_electrical_measurement, ac_power_divisor),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* Without a cmd hook Get Profile Info and Get Measurement Profile get UNSUP_COMMAND */
const struct zzb_zcl_cluster_def zzb_zcl_electrical_measurement_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_electrical_measurement_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
