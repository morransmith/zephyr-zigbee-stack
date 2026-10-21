/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Occupancy Sensing cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_
#define ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Occupancy Sensing cluster (ZCL 4.8). */
#define ZZB_ZCL_OCCUPANCY 0x0406

/** Attributes (ZCL Tables 4-21, 4-25, 4-26 and 4-27). @{ */
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
/** @} */

/** Bit of Occupancy (ZCL 4.8.2.2.1.1). */
#define ZZB_ZCL_OCCUPANCY_OCCUPIED BIT(0)

/** OccupancySensorType (ZCL Table 4-22). */
enum zzb_zcl_occupancy_sensor_type {
	ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_PIR = 0x00,
	ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_ULTRASONIC = 0x01,
	ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_PIR_ULTRASONIC = 0x02,
	ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_PHYSICAL_CONTACT = 0x03,
};

/** Bits of OccupancySensorTypeBitmap (ZCL Table 4-23). @{ */
#define ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP_PIR              BIT(0)
#define ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP_ULTRASONIC       BIT(1)
#define ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP_PHYSICAL_CONTACT BIT(2)
/** @} */

/**
 * The physical contact delays of a sensor that does not report the transition (ZCL 4.8.2.2.4).
 * @{
 */
#define ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_OCCUPIED_TO_UNOCCUPIED_DELAY_NOT_REPORTED 0xffff
#define ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_UNOCCUPIED_TO_OCCUPIED_DELAY_NOT_REPORTED 0xffff
/** @} */

/**
 * Values of an Occupancy Sensing server (ZCL 4.8.2.2). The application fills the first three
 * fields before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only. The
 * delays and thresholds are writable and saved; the application detects occupancy and applies
 * them.
 */
struct zzb_zcl_occupancy {
	/** ZZB_ZCL_OCCUPANCY_OCCUPIED bit. */
	uint8_t occupancy;
	/** enum zzb_zcl_occupancy_sensor_type. */
	uint8_t occupancy_sensor_type;
	/**
	 * ZZB_ZCL_OCCUPANCY_SENSOR_TYPE_BITMAP_* bits, aligned with occupancy_sensor_type as in ZCL
	 * Table 4-24.
	 */
	uint8_t occupancy_sensor_type_bitmap;
	/** Seconds. */
	uint16_t pir_occupied_to_unoccupied_delay;
	uint16_t pir_unoccupied_to_occupied_delay;
	/** Detections within the delay, 0x01 to 0xfe. */
	uint8_t pir_unoccupied_to_occupied_threshold;
	uint16_t ultrasonic_occupied_to_unoccupied_delay;
	uint16_t ultrasonic_unoccupied_to_occupied_delay;
	uint8_t ultrasonic_unoccupied_to_occupied_threshold;
	/** Seconds, or ZZB_ZCL_OCCUPANCY_PHYSICAL_CONTACT_*_DELAY_NOT_REPORTED. */
	uint16_t physical_contact_occupied_to_unoccupied_delay;
	uint16_t physical_contact_unoccupied_to_occupied_delay;
	uint8_t physical_contact_unoccupied_to_occupied_threshold;
};

/** Internal: referenced by the instance macros of the clusters. @{ */
extern const struct zzb_zcl_cluster_def zzb_zcl_occupancy_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_occupancy_client_def;
/** @} */

/** An Occupancy Sensing server with the values v, a pointer to struct zzb_zcl_occupancy. */
#define ZZB_ZCL_OCCUPANCY_SERVER(v)                                                                \
	{                                                                                          \
		.id = ZZB_ZCL_OCCUPANCY,                                                           \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_occupancy *)NULL),                            \
		.def = &zzb_zcl_occupancy_def,                                                     \
	}

/** An Occupancy Sensing client; it accepts reports. */
#define ZZB_ZCL_OCCUPANCY_CLIENT()                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_OCCUPANCY,                                                           \
		.server = false,                                                                   \
		.def = &zzb_zcl_occupancy_client_def,                                              \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_OCCUPANCY_H_ */
