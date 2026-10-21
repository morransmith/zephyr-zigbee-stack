/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZCL_FAKES_H_
#define ZZB_TESTS_UNIT_ZCL_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>
#include <zzb/zcl.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/appliance_control.h>
#include <zzb/zcl/appliance_events.h>
#include <zzb/zcl/appliance_id.h>
#include <zzb/zcl/appliance_stats.h>
#include <zzb/zcl/ballast.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/binary_input.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/device_temp.h>
#include <zzb/zcl/door_lock.h>
#include <zzb/zcl/fan.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/ias_ace.h>
#include <zzb/zcl/ias_wd.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/metering.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/on_off_switch_config.h>
#include <zzb/zcl/poll_control.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/power_profile.h>
#include <zzb/zcl/pump.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zcl/thermostat.h>
#include <zzb/zcl/window_covering.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "nwk/nwk.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_end, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_query_rsp, struct zzb_node *, uint8_t, uint16_t, uint8_t);
/* By default every IEEE address resolves to the requester */
DECLARE_FAKE_VALUE_FUNC(int, zzb_aps_resolve_ext, struct zzb_node *, uint64_t, uint16_t *);
DECLARE_FAKE_VOID_FUNC(zzb_nwk_poll_hold, struct zzb_node *, enum zzb_nwk_hold, bool);
DECLARE_FAKE_VOID_FUNC(zzb_nwk_poll_fast, struct zzb_node *);
DECLARE_FAKE_VOID_FUNC(zzb_nwk_set_poll_intervals, struct zzb_node *, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC(bool, zzb_aps_bound, struct zzb_node *, uint8_t, uint16_t, uint16_t,
			uint8_t);
/* By default apsTrustCenterAddress is TC_EUI64 */
DECLARE_FAKE_VALUE_FUNC(uint64_t, zzb_apsme_get_tc_addr, const struct zzb_node *);

#define NODE_EUI64 0x00124b0001020304ULL
#define TC_EUI64   0x00124b0405060708ULL
#define NODE_ADDR  0x5678

/* The coordinator sends the requests from its endpoint 1 */
#define REQUESTER_ADDR 0x0000
#define REQUESTER_EP   0x01

/*
 * A cluster of the test: a server on endpoints 1 and 2, a client with the same definition on
 * endpoint 3. Endpoint 2 also has the servers of the stack.
 */
#define TEST_CLUSTER 0x0f00

/*
 * An On/Off client without a definition on endpoints 1 and 3; endpoint 3 also has a server.
 * Endpoint 1 has the Identify client of the stack.
 */
#define ON_OFF ZZB_ZCL_ON_OFF

/*
 * Values of the test cluster, attributes 0x0000 to 0x0014 in this order; 0x0000 to 0x0002, 0x0008,
 * 0x000c, 0x0010 and 0x0013 are reportable, 0x0002, 0x0008 to 0x000a and 0x000c to 0x0014 are
 * saved, 0x000b is read-only. A remote write of 0x0004 gets READ_ONLY while bit 0 of 0x000b is
 * set. The reset sets 0x0001, 0x0002 and 0x000f to 0.
 */
struct test_values {
	bool b;
	uint8_t u8;
	uint16_t u16;
	uint8_t e8;
	uint8_t map8;
	const char *str;
	const uint8_t *octstr;
	const char *null_str;
	int16_t i16;
	uint32_t u24;
	uint32_t map32;
	uint16_t map16;
	int8_t i8;
	uint32_t u32;
	uint16_t e16;
	uint64_t eui64;
	int32_t i24;
	int32_t i32;
	uint32_t utc;
	uint64_t u48;
	uint64_t u56;
};

/* ClusterRevision of the test cluster */
#define TEST_REVISION 1

extern struct test_values values1;
extern struct test_values values2;
extern struct test_values values3;

/* The Basic, Identify, Groups, Scenes and On/Off servers on endpoint 2 */
extern struct zzb_zcl_basic basic;
extern struct zzb_zcl_identify identify;
extern struct zzb_zcl_scenes scenes;
extern struct zzb_zcl_on_off on_off;

/* The On/Off server on endpoint 3, which has no Scenes server */
extern struct zzb_zcl_on_off on_off3;

/*
 * A cluster of the test on endpoint 4, with a Scenes server: attributes 0x0000 to 0x0003 of struct
 * test_values. Its scene extension field set is 0x0002 (uint16), 0x0000 (bool), 0x0100 (uint8),
 * which the cluster does not have, and 0x0001 (uint8).
 */
#define SCENE_CLUSTER 0x0f01

extern struct test_values values4;
extern struct zzb_zcl_scenes scenes4;

/* Calls of the recall hook of the scene test cluster, with the arguments of the last one */
struct recall_calls {
	int count;
	uint8_t ep;
	uint32_t set[8];
	size_t set_count;
	uint32_t transition_ds;
};

extern struct recall_calls recall_calls;

/* Endpoint 5 has the client definitions of the stack, except Identify */
#define CLIENT_EP 5

/* The Level Control server on endpoint 10, without other clusters */
extern struct zzb_zcl_level level10;

/*
 * The Scenes, On/Off and Level Control servers on endpoint 11. Both Level Control servers have the
 * factory StartUpCurrentLevel 0xff.
 */
extern struct zzb_zcl_scenes scenes11;
extern struct zzb_zcl_on_off on_off11;
extern struct zzb_zcl_level level11;

/* The Alarms servers on endpoints 20 and 21, which share the alarm table */
extern struct zzb_zcl_alarms alarms20;
extern struct zzb_zcl_alarms alarms21;

/* A cluster of the test on endpoint 20 without attributes and with the alarm_reset hook */
#define ALARM_CLUSTER 0x0f02

extern struct test_values values20;

/* Calls of the alarm_reset hook of the alarm test cluster, with the arguments of the last one */
struct alarm_reset_calls {
	int count;
	uint8_t ep;
	void *values;
	uint8_t code;
};

extern struct alarm_reset_calls alarm_reset_calls;

/*
 * Endpoint 30 has the servers of the power, temperature, ballast and sensing clusters, and an
 * Alarms server
 */
#define SENSOR_EP 30

extern struct zzb_zcl_power_config power_config;
extern struct zzb_zcl_device_temp device_temp;
extern struct zzb_zcl_alarms alarms30;

/* The physical levels of the ballast on endpoint 30 */
#define PHYSICAL_MIN 0x10
#define PHYSICAL_MAX 0xe0

extern struct zzb_zcl_ballast ballast;
extern struct zzb_zcl_illuminance illuminance;
extern struct zzb_zcl_illuminance_level illuminance_level;
extern struct zzb_zcl_occupancy occupancy;

/*
 * The Scenes, On/Off, Level Control and Color Control servers of an Extended Color Light on
 * endpoint 40 (ColorCapabilities 0x0018), and the On/Off, Level Control and Color Control servers
 * of a Color Temperature Light on endpoint 41 (0x0010). Both have this range of color temperature
 * and the factory StartUpColorTemperatureMireds 0xffff.
 */
#define COLOR_TEMP_MIN   153
#define COLOR_TEMP_MAX   500
#define COLOR_COUPLE_MIN 200

extern struct zzb_zcl_scenes scenes40;
extern struct zzb_zcl_on_off on_off40;
extern struct zzb_zcl_level level40;
extern struct zzb_zcl_color color40;
extern struct zzb_zcl_on_off on_off41;
extern struct zzb_zcl_level level41;
extern struct zzb_zcl_color color41;

/* The Scenes and Window Covering servers of a Tilt Blind - Lift and Tilt (0x08) on endpoint 50 */
#define COVERING_EP 50

extern struct zzb_zcl_scenes scenes50;
extern struct zzb_zcl_window_covering covering50;

/*
 * Endpoint 60 is a Thermostat with the Scenes, Thermostat and Thermostat User Interface
 * Configuration servers and a Thermostat client. The Thermostat server has the absolute limits of
 * ZCL Table 6-11, MinSetpointDeadBand 2.5 degrees Celsius, ControlSequenceOfOperation 4, Occupancy
 * 1 and the factory SystemMode Auto.
 */
#define THERMOSTAT_EP 60

extern struct zzb_zcl_scenes scenes60;
extern struct zzb_zcl_thermostat thermostat60;
extern struct zzb_zcl_thermostat_ui thermostat_ui60;

/*
 * Endpoint 70 has the measurement servers, a Fan Control server with FanModeSequence 0x02 and a
 * Pump Configuration and Control server; endpoint 71 has the clients of these clusters
 */
#define HVAC_EP 70

extern struct zzb_zcl_temperature temperature70;
extern struct zzb_zcl_pressure pressure70;
extern struct zzb_zcl_flow flow70;
extern struct zzb_zcl_humidity humidity70;
extern struct zzb_zcl_fan fan70;
extern struct zzb_zcl_pump pump70;

/*
 * Endpoint 80 is a contact switch with an IAS Zone server, ZoneType 0x0015. The device has the
 * manufacturer code 0x1234.
 */
#define ZONE_EP 80

extern struct zzb_zcl_ias_zone zone80;

/* Endpoint 81 is an IAS CIE with the IAS Zone client */
#define ZONE_CLIENT_EP 81

/* Endpoints 85 and 86 are Warning Devices with an IAS WD server each */
#define WD_EP 85

extern struct zzb_zcl_ias_wd wd85;
extern struct zzb_zcl_ias_wd wd86;

/* Endpoint 87 is an IAS CIE with an IAS WD client */
#define WD_CLIENT_EP 87

/* Endpoint 90 is an IAS CIE with the IAS ACE server, endpoint 91 a keypad with the client */
#define CIE_EP    90
#define KEYPAD_EP 91

/*
 * Endpoints 100 and 101 are Door Locks with a Door Lock server, locked and with codes of 4 to 8
 * octets; endpoint 100 also has an Alarms server
 */
#define DOOR_LOCK_EP 100

extern struct zzb_zcl_door_lock lock100;
extern struct zzb_zcl_door_lock lock101;

/* Endpoint 102 is a Door Lock Controller with a Door Lock client */
#define DOOR_LOCK_CLIENT_EP 102

/*
 * Endpoint 120 is an On/Off Switch with the On/Off Switch Configuration server, SwitchType 0x01,
 * and the On/Off client
 */
#define SWITCH_EP 120

extern struct zzb_zcl_on_off_switch_config switch120;

/* Endpoint 121 is a Simple Sensor with the Binary Input server */
#define INPUT_EP 121

extern struct zzb_zcl_binary_input input121;

/* Endpoint 122 is a Meter Interface with the Metering server */
#define METER_EP 122

extern struct zzb_zcl_metering meter122;

/*
 * Endpoint 140 is a White Goods with the Power Profile server and the servers of the EN50523
 * appliance clusters, 141 a Home Gateway with their clients
 */
#define WHITE_GOODS_EP 140
#define GATEWAY_EP     141

extern struct zzb_zcl_power_profile power_profile140;
extern struct zzb_zcl_appliance_control appliance_control140;
extern struct zzb_zcl_appliance_id appliance_id140;
extern struct zzb_zcl_appliance_stats appliance_stats140;

/* Calls of the cmd hook of the test cluster, with a copy of the last command */
struct cmd_calls {
	int count;
	struct zzb_zcl_cmd cmd;
	uint8_t payload[ZZB_BUF_SIZE];
	void *values;
};

extern struct cmd_calls cmd_calls;

/* The value the cmd hook returns, ZZB_ZCL_SUCCESS after reset_fakes() */
extern int cmd_status;

/* The check hook of the test cluster rejects a value of attribute 0x0003 above this */
#define TEST_E8_MAX 0x10

/* The value struct passed to the last call of the check hook of the test cluster */
extern const void *check_values;

/* Calls of the refresh hook of the test cluster, which sets the bits refresh_bits in 0x000b */
extern int refresh_calls;
extern uint16_t refresh_bits;

/* Calls of the changed hook of the test cluster and of zcl_cb, in order */
struct changed_calls {
	int count;
	uint8_t ep[64];
	uint16_t attr[64];
	/* The source endpoint of node->zcl.received at the call of the hook, 0 when NULL */
	uint8_t src_ep[64];
};

extern struct changed_calls hook_calls;

struct zcl_cb_calls {
	int count;
	uint8_t ep[64];
	uint16_t cluster[64];
	uint16_t attr[64];
};

extern struct zcl_cb_calls zcl_cb_calls;

/* zcl_cb of the test nodes, which records its calls in zcl_cb_calls */
void record_zcl_cb(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr);

/* Calls of zcl_effect_cb, with the arguments of the last one */
struct effect_calls {
	int count;
	uint8_t ep;
	uint16_t cluster;
	uint8_t effect;
	uint8_t variant;
	/* zcl_cb_calls.count at the call */
	int zcl_cb_count;
};

extern struct effect_calls effect_calls;

/* Calls of zcl_cmd_cb, with copies of the commands and their payloads */
struct zcl_cmd_calls {
	int count;
	struct zzb_zcl_cmd cmd[8];
	uint8_t payload[8][32];
};

extern struct zcl_cmd_calls zcl_cmd_calls;

/* The status that zcl_cmd_cb returns, ZZB_ZCL_SUCCESS after reset_fakes() */
extern uint8_t zcl_cmd_status;

/*
 * With zcl_cmd_responds, false after reset_fakes(), zcl_cmd_cb answers every command with
 * zzb_zcl_cmd_respond(): command 0x42 with the payload 0xaa. zcl_cmd_respond_err is its result.
 */
extern bool zcl_cmd_responds;
extern int zcl_cmd_respond_err;

/*
 * With zcl_cmd_answer, NULL after reset_fakes(), zcl_cmd_cb answers every command with this
 * function of the test, such as a response function of a cluster header, in place of the answer of
 * zcl_cmd_responds; zcl_cmd_respond_err is its result.
 */
extern int (*zcl_cmd_answer)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd);

/* A membership of the APS group table, kept by the fakes of the group functions; ep 0 is free */
struct membership {
	uint16_t group;
	uint8_t ep;
};

extern struct membership memberships[CONFIG_ZZB_APS_GROUP_COUNT];

/* Copy of a frame passed to APSDE-DATA.request */
struct frame_copy {
	struct zzb_apsde_req req;
	uint8_t handle;
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
};

/* By default the zzb_apsde_data_req() fake takes the buffer and copies the frame here */
extern struct frame_copy sent[8];

struct zzb_node *test_node(void);

/* Resets every fake */
void reset_fakes(void);

/* Resets the fake of APSDE-DATA.request, which copies the frames to sent again */
void reset_frames(void);

/* Checks that every frame buffer is free */
void check_no_buffer_leak(void);

/* Passes the ASDU to ZCL as APSDE-DATA.indication with the parameters of ind */
void receive_ind(const struct zzb_apsde_ind *ind, const uint8_t *asdu, size_t len);

/* Passes a ZCL frame of cluster from the requester to endpoint ep as unicast, profile 0x0104 */
void receive_ep(uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len);

/* receive_ep() to endpoint 1 */
void receive(uint16_t cluster, const uint8_t *asdu, size_t len);

/* Starts ZCL again from the settings records, as zzb_node_init() does, on the stack thread */
int reboot(void);

/* Resets the ZCL attributes as the reset of the node does, on the stack thread */
void reset_node(void);

/* Checks that frame i is a ZCL frame of cluster from endpoint ep to the requester with the ASDU */
void check_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len);

/* Checks that zcl_cb reported these attributes of cluster on endpoint ep in order, and no other */
void check_zcl_cb(uint8_t ep, uint16_t cluster, const uint16_t *attrs, int count);

/*
 * Checks that frame i is a Report Attributes of cluster from endpoint ep to the bound destinations
 * with the attribute records
 */
void check_report_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *records, size_t len);

/* Checks that frame i is Alarm with code of cluster from endpoint ep to the bound destinations */
void check_alarm(int i, uint8_t ep, uint16_t cluster, uint8_t code);

/* The number of entries of the alarm table with ep, cluster and code */
int alarm_entries(uint8_t ep, uint16_t cluster, uint8_t code);

/* Sends Reset Alarm with code of cluster to ep, without a Default Response */
void reset_alarm(uint8_t ep, uint16_t cluster, uint8_t code);

/* Sends Reset All Alarms to ep, without a Default Response */
void reset_all_alarms(uint8_t ep);

#endif /* ZZB_TESTS_UNIT_ZCL_FAKES_H_ */
