/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_ZCL_ZCL_H_
#define ZZB_SUBSYS_ZCL_ZCL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>
#include <zzb/zcl.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "zcl/frame.h"

struct zzb_node;
struct zzb_zcl_thermostat;

/* ASDU handle of ZCL frames; their confirm is ignored */
#define ZZB_ZCL_HANDLE 0xfd

/* Entry of the reporting table: an attribute of a server cluster (ZCL 2.5.7) */
struct zzb_zcl_report {
	bool in_use;
	uint8_t ep;
	uint16_t cluster;
	uint16_t attr;
	/* Seconds */
	uint16_t min;
	uint16_t max;
	/* Reportable change of an analog attribute */
	uint16_t change;
	/* A change waits for the minimum interval */
	bool pending;
	/* The value of the last report */
	uint64_t last;
	/* k_uptime_get() at the last report */
	int64_t sent;
	/* k_uptime_get() at which a report refused off a network is tried again, or 0 */
	int64_t retry;
};

/* The clock of the node (Time); not saved */
struct zzb_zcl_clock {
	bool set;
	/* UTC time at k_uptime_get() uptime */
	uint32_t utc;
	int64_t uptime;
	/* Local time minus UTC time */
	int32_t offset;
};

/* Entry of the scene table (ZCL Table 3-41); names are not stored */
struct zzb_zcl_scene {
	bool in_use;
	uint8_t ep;
	uint16_t group;
	uint8_t id;
	/* Seconds, and tenths of a second from Enhanced Add Scene */
	uint16_t transition;
	uint8_t transition_100ms;
	/* Extension field sets in their ZCL encoding */
	uint8_t data_len;
	uint8_t data[CONFIG_ZZB_ZCL_SCENE_DATA_LEN];
};

/* Entry of the alarm table (ZCL Table 3-66); the time stamp is always 0xffffffff */
struct zzb_zcl_alarm {
	bool in_use;
	/* The earliest entry has the lowest */
	uint32_t seq;
	uint8_t ep;
	uint8_t code;
	uint16_t cluster;
	/* UTC time of the clock of the node, 0xffffffff without time (ZCL Table 3-66) */
	uint32_t time;
};

struct zzb_zcl {
	/* Decrements IdentifyTime every second while one is not 0 */
	struct k_work_delayable identify_timer;
	/* Updates OnTime and OffWaitTime every 0.1 s after On With Timed Off */
	struct k_work_delayable on_off_timer;
	struct zzb_zcl_report reports[CONFIG_ZZB_ZCL_REPORT_COUNT];
	/* Sends the reports when they are due */
	struct k_work_delayable report_timer;
	/* Steps the transitions of Level Control every 100 ms while one runs */
	struct k_work_delayable level_timer;
	/* Steps the transitions of Color Control every 100 ms while one runs */
	struct k_work_delayable color_timer;
	/* Transaction sequence number of the next frame ZCL originates */
	uint8_t tsn;
	struct zzb_zcl_scene scenes[CONFIG_ZZB_ZCL_SCENE_COUNT];
	struct zzb_zcl_alarm alarms[CONFIG_ZZB_ZCL_ALARM_COUNT];
	/* Raises the mains voltage alarms when their dwell has passed */
	struct k_work_delayable mains_alarm_timer;
	/* Raises the device temperature alarms when their dwell has passed */
	struct k_work_delayable temp_alarm_timer;
	/* Ends the warnings of IAS WD after their duration */
	struct k_work_delayable ias_wd_timer;
	/* Tries again the IAS Zone notifications refused off a network */
	struct k_work_delayable ias_zone_timer;
	/* Relocks the Door Locks and ends their lockouts */
	struct k_work_delayable door_lock_timer;
	struct zzb_zcl_clock clock;
	/* zcl_cmd_cb has answered the command it processes */
	bool responded;
	/* The frame being processed, for the hooks; NULL outside the processing of a frame */
	const struct zzb_zcl_cmd *received;
};

/* Global command identifiers (ZCL Table 2-3) */
#define ZZB_ZCL_READ_ATTRIBUTES         0x00
#define ZZB_ZCL_READ_ATTRIBUTES_RSP     0x01
#define ZZB_ZCL_WRITE_ATTRIBUTES        0x02
#define ZZB_ZCL_WRITE_UNDIVIDED         0x03
#define ZZB_ZCL_WRITE_ATTRIBUTES_RSP    0x04
#define ZZB_ZCL_WRITE_NO_RSP            0x05
#define ZZB_ZCL_CONFIGURE_REPORTING     0x06
#define ZZB_ZCL_CONFIGURE_REPORTING_RSP 0x07
#define ZZB_ZCL_READ_REPORTING          0x08
#define ZZB_ZCL_READ_REPORTING_RSP      0x09
#define ZZB_ZCL_REPORT_ATTRIBUTES       0x0a
#define ZZB_ZCL_DEFAULT_RSP             0x0b
#define ZZB_ZCL_DISCOVER_ATTRIBUTES     0x0c
#define ZZB_ZCL_DISCOVER_ATTRIBUTES_RSP 0x0d

/* The longest payload of a response, without the manufacturer code */
#define ZZB_ZCL_MAX_PAYLOAD (ZZB_APS_MAX_ASDU - 3)

/* A received frame for one endpoint */
struct zzb_zcl_req {
	struct zzb_zcl_cmd cmd;
	struct zzb_zcl_hdr hdr;
	/* The profile of the endpoint */
	uint16_t profile;
	/* The instance for the cluster and the direction, or NULL */
	const struct zzb_zcl_cluster *cluster;
};

/*
 * Sends a ZCL frame with hdr and the payload through APSDE-DATA with aps. Returns -EMSGSIZE for a
 * frame longer than ZZB_APS_MAX_ASDU, -ENOBUFS without a buffer or an error of APSDE-DATA.request.
 */
int zzb_zcl_send(struct zzb_node *node, const struct zzb_apsde_req *aps,
		 const struct zzb_zcl_hdr *hdr, const uint8_t *payload, size_t len);

/*
 * Sends the cluster-specific command cmd of a client from endpoint src_ep to dst_addr and dst_ep,
 * with Disable Default Response 1 and an APS ACK request as unicast. Returns -ENOENT for an unknown
 * endpoint or an error of zzb_zcl_send().
 */
int zzb_zcl_send_cmd(struct zzb_node *node, uint8_t src_ep, uint16_t dst_addr, uint8_t dst_ep,
		     uint16_t cluster, uint8_t cmd, const uint8_t *payload, size_t len);

/*
 * Sends a ZCL frame of type with the command identifier cmd from endpoint ep to the bound
 * destinations of cluster, with Disable Default Response 1, the direction to the client when
 * to_client is set and an APS ACK request. Returns an error of zzb_zcl_send().
 */
int zzb_zcl_send_bindings(struct zzb_node *node, const struct zzb_endpoint *ep, uint16_t cluster,
			  enum zzb_zcl_frame_type type, bool to_client, uint8_t cmd,
			  const uint8_t *payload, size_t len);

/*
 * Sends the cluster-specific command cmd of a server from endpoint ep to the IEEE address dst_ext
 * and dst_ep, with Disable Default Response 1 and an APS ACK request. Returns an error of
 * zzb_zcl_send().
 */
int zzb_zcl_send_ext(struct zzb_node *node, const struct zzb_endpoint *ep, uint64_t dst_ext,
		     uint8_t dst_ep, uint16_t cluster, uint8_t cmd, const uint8_t *payload,
		     size_t len);

/*
 * Sends a response to req, a ZCL frame of type with the command identifier cmd and the payload.
 * Returns an error of zzb_zcl_send().
 */
int zzb_zcl_respond(struct zzb_node *node, const struct zzb_zcl_req *req,
		    enum zzb_zcl_frame_type type, uint8_t cmd, const uint8_t *payload, size_t len);

/* The server or client instance of cluster id on ep, or NULL */
const struct zzb_zcl_cluster *zzb_zcl_find_cluster(const struct zzb_endpoint *ep, uint16_t id,
						   bool server);

/* Cancels the timers and sets the defaults of the attributes before the records are loaded */
void zzb_zcl_init(struct zzb_node *node);

/*
 * Writes value to attribute attr of the server cluster on endpoint ep, saves it and calls the
 * changed hook, without zcl_cb
 */
void zzb_zcl_store(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		   uint32_t value);

/*
 * Writes value to attribute attr of the server cluster on endpoint ep and reports a change through
 * the changed hook, the reporting check and zcl_cb, without saving it and without SceneValid
 */
void zzb_zcl_update(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		    uint32_t value);

/* Saves the non-volatile attribute attr of the server cluster on endpoint ep */
void zzb_zcl_save(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr);

/*
 * Starts t on count attributes attrs of cl, at most two, from their values to the values to over
 * time_ds tenths of a second, which is above 0. A running transition is replaced.
 */
void zzb_zcl_transition_start(struct zzb_zcl_transition *t, const struct zzb_zcl_cluster *cl,
			      const uint16_t *attrs, const uint16_t *to, size_t count,
			      uint32_t time_ds);

/*
 * Writes the values of t at the current time to cl on endpoint ep through zzb_zcl_update(). At the
 * end it saves the end values, stops t and returns true. A value that differs from the value t
 * wrote last, because the application has set it, stops t without a write.
 */
bool zzb_zcl_transition_step(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
			     struct zzb_zcl_transition *t);

/* Stops t and saves its attributes of the server cluster on endpoint ep */
void zzb_zcl_transition_stop(struct zzb_node *node, uint8_t ep, uint16_t cluster,
			     struct zzb_zcl_transition *t);

/* The time left of t in tenths of a second, rounded up, at most 0xffff; 0 when t does not run */
uint16_t zzb_zcl_transition_remaining(const struct zzb_zcl_transition *t);

/* Calls the start hooks after the records are loaded */
void zzb_zcl_start(struct zzb_node *node);

void zzb_zcl_identify_init(struct zzb_node *node);

/*
 * Sets IdentifyTime of the Identify server on ep, with zcl_cb, for finding & binding. Returns
 * -ENOENT without the server.
 */
int zzb_zcl_identify(struct zzb_node *node, uint8_t ep, uint16_t time);
void zzb_zcl_color_init(struct zzb_node *node);

/*
 * Sets ColorTemperatureMireds of the Color Control server on endpoint ep, if there is one, for
 * CurrentLevel level in color temperature mode, through zzb_zcl_update() (ZCL 5.2.2.1.1)
 */
void zzb_zcl_color_level(struct zzb_node *node, uint8_t ep, uint8_t level);
void zzb_zcl_level_init(struct zzb_node *node);
void zzb_zcl_on_off_init(struct zzb_node *node);

/*
 * Applies Off, On or Toggle, the command cmd of the On/Off server on endpoint ep, to the level of
 * the Level Control server on ep (ZCL Table 3-55), which also sets OnOff. Returns -ENOENT without
 * the server and -EALREADY when the level does not change; the On/Off server then applies the
 * command itself.
 */
int zzb_zcl_level_on_off(struct zzb_node *node, uint8_t ep, uint8_t cmd);

/*
 * Sets OnOff of the On/Off server on endpoint ep, if there is one, with the rules of On or Off for
 * GlobalSceneControl, OnTime and OffWaitTime; for Level Control (ZCL 3.8.2.1.1, 3.10.2.1.3)
 */
void zzb_zcl_on_off_switch(struct zzb_node *node, uint8_t ep, bool on_off);

void zzb_zcl_power_config_init(struct zzb_node *node);
void zzb_zcl_device_temp_init(struct zzb_node *node);
void zzb_zcl_ias_wd_init(struct zzb_node *node);
void zzb_zcl_ias_zone_init(struct zzb_node *node);
void zzb_zcl_door_lock_init(struct zzb_node *node);
void zzb_zcl_report_init(struct zzb_node *node);

/*
 * Loads the record zcl/rep/<index>; -EINVAL for an unknown version or length. A record of an
 * attribute that the device definition does not have as reportable is ignored.
 */
int zzb_zcl_report_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			void *cb_arg);

/* Takes the current values as the values of the last reports after the records are loaded */
void zzb_zcl_report_start(struct zzb_node *node);

/* Clears the reporting table, stops its timer and deletes the records */
void zzb_zcl_report_reset(struct zzb_node *node);

/* Marks a report due when the new value of attribute attr of cl on endpoint ep needs one */
void zzb_zcl_report_check(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
			  uint16_t attr);

/* The cmd hook of the client definitions: responses and Alarm need no processing */
int zzb_zcl_client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values);

/* Processes a global command; returns the status of the Default Response or ZZB_ZCL_RESPONDED */
int zzb_zcl_foundation(struct zzb_node *node, const struct zzb_zcl_req *req);

/* The attribute id of cl, or NULL */
const struct zzb_zcl_attr *zzb_zcl_attr_find(const struct zzb_zcl_cluster *cl, uint16_t id);

/* The value of an attribute of an integer type other than EUI64, with the bits of its ZCL value */
uint64_t zzb_zcl_attr_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr);

/* Writes the value of attr of cl in its ZCL encoding; returns its length or -ENOSPC */
int zzb_zcl_attr_encode(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint8_t *p, size_t len);

/*
 * Checks 2 to 6 of ZCL 2.5.3.3 for a remote write of the value of type at p to attr of cl. Returns
 * the status and, with ZZB_ZCL_SUCCESS, the value in *value.
 */
uint8_t zzb_zcl_attr_check(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint8_t type, const uint8_t *p, uint64_t *value);

/* Writes value to the field of attr of cl; returns whether the value changed */
bool zzb_zcl_attr_write(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint64_t value);

/*
 * Loads the record zcl/<key> of a non-volatile attribute; -EINVAL for an unknown version or
 * length. A record of an attribute that the device definition does not have is ignored.
 */
int zzb_zcl_attr_load(struct zzb_node *node, const char *key, size_t len, settings_read_cb read_cb,
		      void *cb_arg);

/* Deletes the records of the non-volatile attributes */
void zzb_zcl_attr_delete(struct zzb_node *node);

/* Saves a non-volatile attribute, then calls the changed hook of cl and zcl_cb for attr */
void zzb_zcl_changed(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		     uint16_t attr);

/* Writes value to attribute attr of the server cluster on endpoint ep and reports a change */
void zzb_zcl_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		 uint32_t value);

/*
 * Restores the defaults of the attributes through the reset hooks, which report each change, and
 * deletes the records of the non-volatile attributes
 */
void zzb_zcl_reset(struct zzb_node *node);

/*
 * Sends the cluster-specific response id with the payload to cmd, which a cmd hook processes.
 * Returns an error of zzb_zcl_send().
 */
int zzb_zcl_cmd_reply(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t id,
		      const uint8_t *payload, size_t len);

/* Calls zcl_effect_cb */
void zzb_zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		    uint8_t variant);

/*
 * Passes cmd to zcl_cmd_cb; returns its status, ZZB_ZCL_RESPONDED when the application has answered
 * the command, or ZZB_ZCL_SUCCESS without zcl_cmd_cb
 */
int zzb_zcl_app_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd);

/* Sets the clock of the node to the UTC time utc and the local time utc + offset */
void zzb_zcl_clock_set(struct zzb_node *node, uint32_t utc, int32_t offset);

/* The UTC time of the clock of the node, or 0xffffffff without time */
uint32_t zzb_zcl_clock_utc(const struct zzb_node *node);

/* The local time of the clock of the node, or 0xffffffff without time */
uint32_t zzb_zcl_clock_local(const struct zzb_node *node);

#define ZZB_ZCL_FIELDS_MAX 3

/* A command that a cluster passes to zcl_cmd_cb and its fields */
struct zzb_zcl_command {
	uint8_t id;
	struct zzb_zcl_field fields[ZZB_ZCL_FIELDS_MAX];
};

/*
 * Passes cmd to zcl_cmd_cb when it is in the table of count commands and its payload holds their
 * fields; octets after them are ignored (ZCL 2.3.2). Returns MALFORMED_COMMAND for a shorter
 * payload, UNSUP_COMMAND for a command missing from the table, otherwise as zzb_zcl_app_cmd().
 */
int zzb_zcl_pass(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		 const struct zzb_zcl_command *commands, size_t count);

/* Configure Reporting (ZCL 2.5.7); returns the Default Response status or ZZB_ZCL_RESPONDED */
int zzb_zcl_configure_reporting(struct zzb_node *node, const struct zzb_zcl_req *req);

/* Read Reporting Configuration (ZCL 2.5.9); returns ZZB_ZCL_RESPONDED */
int zzb_zcl_read_reporting(struct zzb_node *node, const struct zzb_zcl_req *req);

/*
 * Sets SceneValid of the Scenes server on endpoint ep FALSE after a change of an attribute of a
 * scene (ZCL 3.7.2.2.1.4), with zcl_cb when notify is set
 */
void zzb_zcl_scenes_invalidate(struct zzb_node *node, uint8_t ep, bool notify);

/*
 * Loads the record zcl/scene/<index>; -EINVAL for an unknown version or length. A record of an
 * endpoint without a Scenes server is ignored.
 */
int zzb_zcl_scenes_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			void *cb_arg);

/* Deletes the records of the scene table */
void zzb_zcl_scenes_delete(struct zzb_node *node);

/*
 * Stores the current state of endpoint ep as scene id of group (ZCL 3.7.2.4.6) and makes it the
 * current scene. Returns ZZB_ZCL_SUCCESS or ZZB_ZCL_INSUFFICIENT_SPACE.
 */
uint8_t zzb_zcl_scene_store(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id);

/*
 * Recalls scene id of group on endpoint ep (ZCL 3.7.2.4.7) and makes it the current scene. A
 * transition time of 0xffff takes the time of the scene. Returns ZZB_ZCL_SUCCESS or
 * ZZB_ZCL_NOT_FOUND.
 */
uint8_t zzb_zcl_scene_recall(struct zzb_node *node, uint8_t ep, uint16_t group, uint8_t id,
			     uint16_t transition_ds);

/* Removes the scenes of group on endpoint ep (ZCL 3.6.2.3.5) */
void zzb_zcl_scenes_remove_group(struct zzb_node *node, uint8_t ep, uint16_t group);

/*
 * Enters the alarm code of cluster into the alarm table when endpoint ep has an Alarms server
 * (ZCL 3.11.2.3), with zcl_cb for AlarmCount when notify is set, and sends it as Alarm to the bound
 * destinations of the Alarms cluster when send is set
 */
void zzb_zcl_alarm_raise(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t code,
			 bool send, bool notify);

/*
 * Loads the record zcl/alarm/<index>; -EINVAL for an unknown version or length. A record of an
 * endpoint without an Alarms server is ignored.
 */
int zzb_zcl_alarms_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			void *cb_arg);

/* Clears the alarm table, sets AlarmCount 0 with zcl_cb and deletes the records */
void zzb_zcl_alarms_reset(struct zzb_node *node);

/*
 * Set Weekly Schedule (ZCL 6.3.2.3.2) to the Thermostat server with the values v; returns the
 * status of the Default Response
 */
int zzb_zcl_schedule_set(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_thermostat *v);

/*
 * Get Weekly Schedule (ZCL 6.3.2.3.3) to the Thermostat server with the values v; returns the
 * status of the Default Response or ZZB_ZCL_RESPONDED
 */
int zzb_zcl_schedule_get(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 const struct zzb_zcl_thermostat *v);

/*
 * Empties every day of the weekly schedule of the Thermostat server on ep with the values v and
 * deletes their records
 */
void zzb_zcl_schedule_clear(struct zzb_node *node, uint8_t ep, struct zzb_zcl_thermostat *v);

/*
 * Loads the record zcl/sched/<key>, key being <ep>/<day>; -EINVAL for an unknown version, a count
 * above the capacity or a length other than that of the count. A record of an endpoint without a
 * Thermostat server or of a day above 7 is ignored.
 */
int zzb_zcl_schedule_load(struct zzb_node *node, const char *key, size_t len,
			  settings_read_cb read_cb, void *cb_arg);

/*
 * Loads the record zcl/ias/<ep>, the endpoint of the CIE of the IAS Zone server on ep; -EINVAL for
 * an unknown version or length. A record of an endpoint without an IAS Zone server is ignored.
 */
int zzb_zcl_ias_zone_load(struct zzb_node *node, int ep, size_t len, settings_read_cb read_cb,
			  void *cb_arg);

/*
 * Loads the record zcl/lock/<key>, key being <ep>/<user>; -EINVAL for an unknown version or length
 * or a code longer than ZZB_ZCL_DOOR_LOCK_PIN_MAX. A record of an endpoint without a Door Lock
 * server or of a user outside its table is ignored.
 */
int zzb_zcl_door_lock_load(struct zzb_node *node, const char *key, size_t len,
			   settings_read_cb read_cb, void *cb_arg);

/* Processes a frame of APSDE-DATA.indication for an endpoint other than 0, and takes buf */
void zzb_zcl_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind);

#endif /* ZZB_SUBSYS_ZCL_ZCL_H_ */
