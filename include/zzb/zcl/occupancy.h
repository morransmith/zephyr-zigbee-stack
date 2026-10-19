/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_
#define ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Occupancy Sensing cluster (ZCL 4.8). */
#define ZZB_ZCL_OCCUPANCY 0x0406

/** Attributes (ZCL Tables 4-21, 4-25, 4-26 and 4-27). */
#define ZZB_ZCL_OCCUPANCY_OCCUPANCY                                         0x0000
#define ZZB_ZCL_OCCUPANCY_SENSOR_TYPE                                       0x0001
#define ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP                                0x0002
#define ZZB_ZCL_OCCUPANCY_PIR_OCCUPIED_TO_UNOCCUPIED_DELAY                  0x0010
#define ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_DELAY                  0x0011
#define ZZB_ZCL_OCCUPANCY_PIR_UNOCCUPIED_TO_OCCUPIED_THRESHOLD              0x0012
#define ZZB_ZCL_OCCUPANCY_ULTRASONIC_OCCUPIED_TO_UNOCCUPIED_DELAY           0x0020
#define ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_DELAY           0x0021
#define ZZB_ZCL_OCCUPANCY_ULTRASONIC_UNOCCUPIED_TO_OCCUPIED_THRESHOLD       0x0022
#define ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_OCCUPIED_TO_UNOCCUPIED_DELAY     0x0030
#define ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_DELAY     0x0031
#define ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_THRESHOLD 0x0032

/**
 * Values of an Occupancy Sensing server (ZCL 4.8.2.2). The application fills the first three
 * fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. The
 * delays and thresholds are writable and saved; the application detects occupancy and applies
 * them.
 */
struct zzb_zcl_occupancy {
	/** Bit 0: occupied. */
	uint8_t occupancy;
	/** ZCL Table 4-22. */
	uint8_t occupancy_sensor_type;
	/** ZCL Table 4-23, aligned with occupancy_sensor_type as in ZCL Table 4-24. */
	uint8_t occupancy_sensor_type_bitmap;
	/** Seconds. */
	uint16_t pir_occupied_to_unoccupied_delay;
	uint16_t pir_unoccupied_to_occupied_delay;
	/** Detections within the delay, 0x01 to 0xfe. */
	uint8_t pir_unoccupied_to_occupied_threshold;
	uint16_t ultrasonic_occupied_to_unoccupied_delay;
	uint16_t ultrasonic_unoccupied_to_occupied_delay;
	uint8_t ultrasonic_unoccupied_to_occupied_threshold;
	/** 0xffff: the sensor does not report the transition. */
	uint16_t physical_contact_occupied_to_unoccupied_delay;
	uint16_t physical_contact_unoccupied_to_occupied_delay;
	uint8_t physical_contact_unoccupied_to_occupied_threshold;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_occupancy_def;

/** An Occupancy Sensing server with the values v, a pointer to struct zzb_zcl_occupancy. */
#define ZZB_ZCL_OCCUPANCY_SERVER(v)                                                                \
	{                                                                                          \
		.id = ZZB_ZCL_OCCUPANCY,                                                           \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_occupancy *)NULL),                            \
		.def = &zzb_zcl_occupancy_def,                                                     \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_ */
