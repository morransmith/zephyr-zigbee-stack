/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_ZCL_FAKES_H_
#define ZZB_TESTS_UNIT_ZCL_FAKES_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>
#include <zzb/zcl.h>
#include <zzb/zcl/alarms.h>
#include <zzb/zcl/ballast.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/color.h>
#include <zzb/zcl/device_temp.h>
#include <zzb/zcl/fan.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/illuminance.h>
#include <zzb/zcl/level.h>
#include <zzb/zcl/measurement.h>
#include <zzb/zcl/occupancy.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/scenes.h>

#include "aps/aps.h"
#include "common/buf.h"

DECLARE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
			const struct zzb_apsde_req *);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_end, struct zzb_node *, uint8_t);
DECLARE_FAKE_VOID_FUNC(zzb_bdb_identify_query_rsp, struct zzb_node *, uint8_t, uint16_t, uint8_t);

#define NODE_EUI64 0x00124b0001020304ULL
#define NODE_ADDR  0x5678

/* The coordinator sends the requests from its endpoint 1 */
#define REQUESTER_ADDR 0x0000
#define REQUESTER_EP   0x01

/*
 * A cluster of the test: a server on endpoints 1 and 2, not on endpoint 3. Endpoint 2 also has the
 * servers of the stack.
 */
#define TEST_CLUSTER 0x0f00

/*
 * An On/Off client without a definition on endpoints 1 and 3; endpoint 3 also has a server.
 * Endpoint 1 has the Identify client of the stack.
 */
#define ON_OFF ZZB_ZCL_ON_OFF

/*
 * Values of the test cluster, attributes 0x0000 to 0x000d in this order; 0x0000 to 0x0002, 0x0008
 * and 0x000c are reportable, 0x0002, 0x0008 to 0x000a, 0x000c and 0x000d are saved, 0x000b is
 * read-only. The reset sets 0x0001 and 0x0002 to 0.
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
};

/* ClusterRevision of the test cluster */
#define TEST_REVISION 1

extern struct test_values values1;
extern struct test_values values2;

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

/* Endpoint 70 has the measurement servers and a Fan Control server with FanModeSequence 0x02 */
#define HVAC_EP 70

extern struct zzb_zcl_temperature temperature70;
extern struct zzb_zcl_pressure pressure70;
extern struct zzb_zcl_flow flow70;
extern struct zzb_zcl_humidity humidity70;
extern struct zzb_zcl_fan fan70;

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

/* Calls of the changed hook of the test cluster and of zcl_cb, in order */
struct changed_calls {
	int count;
	uint8_t ep[64];
	uint16_t attr[64];
};

extern struct changed_calls hook_calls;

struct zcl_cb_calls {
	int count;
	uint8_t ep[64];
	uint16_t cluster[64];
	uint16_t attr[64];
};

extern struct zcl_cb_calls zcl_cb_calls;

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

/* Checks that frame i is Alarm with code of cluster from endpoint ep to the bound destinations */
void check_alarm(int i, uint8_t ep, uint16_t cluster, uint8_t code);

/* The number of entries of the alarm table with ep, cluster and code */
int alarm_entries(uint8_t ep, uint16_t cluster, uint8_t code);

/* Sends Reset Alarm with code of cluster to ep, without a Default Response */
void reset_alarm(uint8_t ep, uint16_t cluster, uint8_t code);

/* Sends Reset All Alarms to ep, without a Default Response */
void reset_all_alarms(uint8_t ep);

#endif /* ZZB_TESTS_UNIT_ZCL_FAKES_H_ */
