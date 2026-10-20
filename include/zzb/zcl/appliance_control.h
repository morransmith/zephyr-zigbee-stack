/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_CONTROL_H_
#define ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_CONTROL_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EN50523 Appliance Control cluster (ZCL 15.2). */
#define ZZB_ZCL_APPLIANCE_CONTROL 0x001b

/** Attributes (ZCL Table 15-3). */
#define ZZB_ZCL_APPLIANCE_CONTROL_START_TIME     0x0000
#define ZZB_ZCL_APPLIANCE_CONTROL_FINISH_TIME    0x0001
#define ZZB_ZCL_APPLIANCE_CONTROL_REMAINING_TIME 0x0002

/** Commands that the server receives and a client sends (ZCL Table 15-5). */
#define ZZB_ZCL_APPLIANCE_CONTROL_EXECUTION_OF_COMMAND  0x00
#define ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE          0x01
#define ZZB_ZCL_APPLIANCE_CONTROL_WRITE_FUNCTIONS       0x02
#define ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_PAUSE_RESUME 0x03
#define ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_PAUSE        0x04
#define ZZB_ZCL_APPLIANCE_CONTROL_OVERLOAD_WARNING      0x05

/** Commands that the server generates and a client receives (ZCL Table 15-8). */
#define ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE_RSP          0x00
#define ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE_NOTIFICATION 0x01

/**
 * Command Identification of Execution of a Command (ZCL Table 15-6); 0x80 to 0xff are
 * manufacturer-specific.
 */
enum zzb_zcl_appliance_command {
	ZZB_ZCL_APPLIANCE_START = 0x01,
	ZZB_ZCL_APPLIANCE_STOP = 0x02,
	ZZB_ZCL_APPLIANCE_PAUSE = 0x03,
	ZZB_ZCL_APPLIANCE_START_SUPERFREEZING = 0x04,
	ZZB_ZCL_APPLIANCE_STOP_SUPERFREEZING = 0x05,
	ZZB_ZCL_APPLIANCE_START_SUPERCOOLING = 0x06,
	ZZB_ZCL_APPLIANCE_STOP_SUPERCOOLING = 0x07,
	ZZB_ZCL_APPLIANCE_DISABLE_GAS = 0x08,
	ZZB_ZCL_APPLIANCE_ENABLE_GAS = 0x09,
};

/** Warning Event of Overload Warning (ZCL Table 15-7). */
enum zzb_zcl_appliance_warning {
	ZZB_ZCL_APPLIANCE_ABOVE_AVAILABLE_POWER = 0x00,
	ZZB_ZCL_APPLIANCE_ABOVE_POWER_THRESHOLD = 0x01,
	ZZB_ZCL_APPLIANCE_BELOW_AVAILABLE_POWER = 0x02,
	ZZB_ZCL_APPLIANCE_BELOW_POWER_THRESHOLD = 0x03,
	ZZB_ZCL_APPLIANCE_ABOVE_AVAILABLE_POWER_AT_START = 0x04,
};

/**
 * Appliance Status of Signal State Response and Notification (ZCL Table 15-9); 0x80 to 0xff are
 * manufacturer-specific.
 */
enum zzb_zcl_appliance_status {
	ZZB_ZCL_APPLIANCE_OFF = 0x01,
	ZZB_ZCL_APPLIANCE_STAND_BY = 0x02,
	ZZB_ZCL_APPLIANCE_PROGRAMMED = 0x03,
	ZZB_ZCL_APPLIANCE_PROGRAMMED_WAITING_TO_START = 0x04,
	ZZB_ZCL_APPLIANCE_RUNNING = 0x05,
	ZZB_ZCL_APPLIANCE_PAUSED = 0x06,
	ZZB_ZCL_APPLIANCE_END_PROGRAMMED = 0x07,
	ZZB_ZCL_APPLIANCE_FAILURE = 0x08,
	ZZB_ZCL_APPLIANCE_PROGRAMME_INTERRUPTED = 0x09,
	ZZB_ZCL_APPLIANCE_IDLE = 0x0a,
	ZZB_ZCL_APPLIANCE_RINSE_HOLD = 0x0b,
	ZZB_ZCL_APPLIANCE_SERVICE = 0x0c,
	ZZB_ZCL_APPLIANCE_SUPERFREEZING = 0x0d,
	ZZB_ZCL_APPLIANCE_SUPERCOOLING = 0x0e,
	ZZB_ZCL_APPLIANCE_SUPERHEATING = 0x0f,
};

/** Remote Enable Flags in bits 0 to 3 of the second field of Signal State (ZCL Table 15-10). */
enum zzb_zcl_appliance_remote {
	ZZB_ZCL_APPLIANCE_REMOTE_DISABLED = 0x0,
	ZZB_ZCL_APPLIANCE_REMOTE_AND_ENERGY_CONTROL = 0x1,
	ZZB_ZCL_APPLIANCE_REMOTE_TEMPORARILY_LOCKED = 0x7,
	ZZB_ZCL_APPLIANCE_REMOTE_CONTROL = 0xf,
};

/**
 * Values of an Appliance Control server (ZCL 15.2.3). The application fills them before
 * zzb_node_init() and changes them with zzb_zcl_attr_set(); they are read-only and reportable. The
 * times have the encoding of ZCL Table 15-4: minutes in bits 0 to 5, 0 for relative or 1 for
 * absolute time in bits 6 and 7, hours in bits 8 to 15.
 */
struct zzb_zcl_appliance_control {
	uint16_t start_time;
	uint16_t finish_time;
	/** Relative; 0 outside the RUNNING state. */
	uint16_t remaining_time;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_control_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_appliance_control_client_def;

/**
 * An Appliance Control server with the values v, a pointer to struct zzb_zcl_appliance_control,
 * which the White Goods device has (DTL). Its commands go to zcl_cmd_cb after the length checks,
 * Write Functions after the check of its records by the lengths of their types. The application
 * carries out the commands and the functions, answers Signal State with zzb_zcl_cmd_respond() and
 * sends Signal State Notification with zzb_zcl_send_bound().
 */
#define ZZB_ZCL_APPLIANCE_CONTROL_SERVER(v)                                                        \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_CONTROL,                                                   \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_appliance_control *)NULL),                    \
		.def = &zzb_zcl_appliance_control_def,                                             \
	}

/**
 * An Appliance Control client. Signal State Response and Signal State Notification go to
 * zcl_cmd_cb after the length check; a payload of 5 octets or more carries Appliance Status 2.
 */
#define ZZB_ZCL_APPLIANCE_CONTROL_CLIENT()                                                         \
	{                                                                                          \
		.id = ZZB_ZCL_APPLIANCE_CONTROL,                                                   \
		.server = false,                                                                   \
		.def = &zzb_zcl_appliance_control_client_def,                                      \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_APPLIANCE_CONTROL_H_ */
