/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Power Configuration cluster.
 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_
#define ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power Configuration cluster (ZCL 3.3). */
#define ZZB_ZCL_POWER_CONFIG 0x0001

/** Attributes of the mains and of the first battery source (ZCL 3.3.2.2). @{ */
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE                    0x0000
#define ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY                  0x0001
#define ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK                 0x0010
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD      0x0011
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD      0x0012
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DWELL_TRIP_POINT   0x0013
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE                  0x0020
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING     0x0021
#define ZZB_ZCL_POWER_CONFIG_BATTERY_MANUFACTURER             0x0030
#define ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE                     0x0031
#define ZZB_ZCL_POWER_CONFIG_BATTERY_AHR_RATING               0x0032
#define ZZB_ZCL_POWER_CONFIG_BATTERY_QUANTITY                 0x0033
#define ZZB_ZCL_POWER_CONFIG_BATTERY_RATED_VOLTAGE            0x0034
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK               0x0035
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD    0x0036
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1       0x0037
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD2       0x0038
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD3       0x0039
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_MIN_THRESHOLD 0x003a
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD1    0x003b
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD2    0x003c
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_THRESHOLD3    0x003d
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE              0x003e
/** @} */

/** Special values of the measurements (ZCL 3.3.2.2.1, 3.3.2.2.3). @{ */
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_UNKNOWN                0xffff
#define ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY_TOO_LOW              0x00
#define ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY_TOO_HIGH             0xfe
#define ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY_UNKNOWN              0xff
#define ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_UNKNOWN              0xff
#define ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_UNKNOWN 0xff
/** @} */

/** Thresholds that raise no alarm (ZCL 3.3.2.2.2, 3.3.2.2.4). @{ */
#define ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_THRESHOLD_NONE 0xffff
#define ZZB_ZCL_POWER_CONFIG_BATTERY_THRESHOLD_NONE       0xff
/** @} */

/** Bits of MainsAlarmMask (ZCL Table 3-20). @{ */
#define ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_LOW  BIT(0)
#define ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH BIT(1)
#define ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_SUPPLY_LOST  BIT(2)
/** @} */

/** BatterySize (ZCL Table 3-23). */
enum zzb_zcl_power_config_battery_size {
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_NONE = 0x00,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_BUILT_IN = 0x01,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_OTHER = 0x02,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA = 0x03,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AAA = 0x04,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_C = 0x05,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_D = 0x06,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_CR2 = 0x07,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_CR123A = 0x08,
	ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_UNKNOWN = 0xff,
};

/** Bits of BatteryAlarmMask (ZCL Table 3-24). @{ */
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK_MIN_THRESHOLD BIT(0)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK_THRESHOLD1    BIT(1)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK_THRESHOLD2    BIT(2)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK_THRESHOLD3    BIT(3)
/** @} */

/** Bits of BatteryAlarmState (ZCL Table 3-26). @{ */
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE1_MIN_THRESHOLD BIT(0)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE1_THRESHOLD1    BIT(1)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE1_THRESHOLD2    BIT(2)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE1_THRESHOLD3    BIT(3)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE2_MIN_THRESHOLD BIT(10)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE2_THRESHOLD1    BIT(11)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE2_THRESHOLD2    BIT(12)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE2_THRESHOLD3    BIT(13)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE3_MIN_THRESHOLD BIT(20)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE3_THRESHOLD1    BIT(21)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE3_THRESHOLD2    BIT(22)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_SOURCE3_THRESHOLD3    BIT(23)
#define ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_MAINS_SUPPLY_LOST     BIT(30)
/** @} */

/**
 * Values of a Power Configuration server (ZCL 3.3.2.2). The application fills the fields up to
 * battery_rated_voltage before zzb_node_init() and changes them with zzb_zcl_attr_set(); they are
 * read-only. The stack keeps the other fields; the alarm settings are writable and saved.
 */
struct zzb_zcl_power_config {
	/** Units of 100 mV, or ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_UNKNOWN. */
	uint16_t mains_voltage;
	/**
	 * Half the frequency in Hz, or ZZB_ZCL_POWER_CONFIG_MAINS_FREQUENCY_* (ZCL 3.3.2.2.1.2).
	 */
	uint8_t mains_frequency;
	/** Units of 100 mV, or ZZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_UNKNOWN. */
	uint8_t battery_voltage;
	/** Units of 0.5 %, or ZZB_ZCL_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_UNKNOWN. */
	uint8_t battery_percentage_remaining;
	const char *battery_manufacturer;
	/** enum zzb_zcl_power_config_battery_size. */
	uint8_t battery_size;
	/** Units of 10 mAh. */
	uint16_t battery_ahr_rating;
	uint8_t battery_quantity;
	/** Units of 100 mV. */
	uint8_t battery_rated_voltage;
	/** ZZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_* bits. */
	uint8_t mains_alarm_mask;
	/** Units of 100 mV, or ZZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_THRESHOLD_NONE. */
	uint16_t mains_voltage_min_threshold;
	uint16_t mains_voltage_max_threshold;
	/** Seconds. */
	uint16_t mains_voltage_dwell_trip_point;
	/** ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_MASK_* bits. */
	uint8_t battery_alarm_mask;
	/** Units of 100 mV, or ZZB_ZCL_POWER_CONFIG_BATTERY_THRESHOLD_NONE. */
	uint8_t battery_voltage_min_threshold;
	uint8_t battery_voltage_threshold1;
	uint8_t battery_voltage_threshold2;
	uint8_t battery_voltage_threshold3;
	/** Percent, or ZZB_ZCL_POWER_CONFIG_BATTERY_THRESHOLD_NONE. */
	uint8_t battery_percentage_min_threshold;
	uint8_t battery_percentage_threshold1;
	uint8_t battery_percentage_threshold2;
	uint8_t battery_percentage_threshold3;
	/**
	 * ZZB_ZCL_POWER_CONFIG_BATTERY_ALARM_STATE_* bits. Bits 0 to 3 follow the battery alarms;
	 * the application may set the other bits with zzb_zcl_attr_set() and keeps bits 0 to 3 as
	 * they are.
	 */
	uint32_t battery_alarm_state;
	/** Internal: bit n while the condition of battery alarm 0x10 + n lasts. */
	uint8_t battery_active;
	/** Internal: bit n while the condition of mains voltage alarm n lasts. */
	uint8_t mains_active;
	/** Internal: bit n once mains voltage alarm n is raised during its condition. */
	uint8_t mains_raised;
	/** Internal: k_uptime_get() at the start of the condition of mains voltage alarm n. */
	int64_t mains_since[2];
};

extern const struct zzb_zcl_cluster_def zzb_zcl_power_config_def;

/** A Power Configuration server with the values v, a pointer to struct zzb_zcl_power_config. */
#define ZZB_ZCL_POWER_CONFIG_SERVER(v)                                                             \
	{                                                                                          \
		.id = ZZB_ZCL_POWER_CONFIG,                                                        \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_power_config *)NULL),                         \
		.def = &zzb_zcl_power_config_def,                                                  \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_POWER_CONFIG_H_ */
