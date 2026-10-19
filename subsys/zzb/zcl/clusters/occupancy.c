/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/occupancy.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 4.8.1.1) */
#define REVISION 2

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_OCCUPANCY, ZZB_ZCL_MAP8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_occupancy, occupancy),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_SENSOR_TYPE, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_occupancy,
		     occupancy_sensor_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP, ZZB_ZCL_MAP8, 0,
		     struct zzb_zcl_occupancy, occupancy_sensor_type_bitmap),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PIR_OCCUPIED_TO_UNOCCUPIED_DELAY, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     pir_occupied_to_unoccupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_DELAY, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     pir_unoccupied_to_occupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_THRESHOLD, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     pir_unoccupied_to_occupied_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_ULTRASONIC_OCCUPIED_TO_UNOCCUPIED_DELAY, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     ultrasonic_occupied_to_unoccupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_DELAY, ZZB_ZCL_UINT16,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     ultrasonic_unoccupied_to_occupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_THRESHOLD, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     ultrasonic_unoccupied_to_occupied_threshold),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_OCCUPIED_TO_UNOCCUPIED_DELAY,
		     ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     physical_contact_occupied_to_unoccupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_DELAY,
		     ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     physical_contact_unoccupied_to_occupied_delay),
	ZZB_ZCL_ATTR(ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_THRESHOLD,
		     ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_occupancy,
		     physical_contact_unoccupied_to_occupied_threshold),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL Tables 4-25 to 4-27 */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	switch (attr->id) {
	case ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_THRESHOLD:
	case ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_THRESHOLD:
	case ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_THRESHOLD:
		return value >= 0x01 && value <= 0xfe ? ZZB_ZCL_SUCCESS : ZZB_ZCL_INVALID_VALUE;
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

/* The defaults of ZCL 4.8.2.2; the fields of the application keep their values */
static void init(void *values)
{
	struct zzb_zcl_occupancy *v = values;

	v->pir_occupied_to_unoccupied_delay = 0;
	v->pir_unoccupied_to_occupied_delay = 0;
	v->pir_unoccupied_to_occupied_threshold = 1;
	v->ultrasonic_occupied_to_unoccupied_delay = 0;
	v->ultrasonic_unoccupied_to_occupied_delay = 0;
	v->ultrasonic_unoccupied_to_occupied_threshold = 1;
	v->physical_contact_occupied_to_unoccupied_delay = 0;
	v->physical_contact_unoccupied_to_occupied_delay = 0;
	v->physical_contact_unoccupied_to_occupied_threshold = 1;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_OCCUPANCY, attr, value);
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	set(node, ep, ZZB_ZCL_OCCUPANCY_PIR_OCCUPIED_TO_UNOCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_THRESHOLD, 1);
	set(node, ep, ZZB_ZCL_OCCUPANCY_ULTRASONIC_OCCUPIED_TO_UNOCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_THRESHOLD, 1);
	set(node, ep, ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_OCCUPIED_TO_UNOCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_DELAY, 0);
	set(node, ep, ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_THRESHOLD, 1);
}

const struct zzb_zcl_cluster_def zzb_zcl_occupancy_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.check = check,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

const struct zzb_zcl_cluster_def zzb_zcl_occupancy_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = zzb_zcl_client_cmd,
};
