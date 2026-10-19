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
	/* The value of the last report */
	uint16_t last;
	/* k_uptime_get() at the last report */
	int64_t sent;
};

struct zzb_zcl {
	/* Decrements IdentifyTime every second while one is not 0 */
	struct k_work_delayable identify_timer;
	/* Updates OnTime and OffWaitTime every 0.1 s after On With Timed Off */
	struct k_work_delayable on_off_timer;
	struct zzb_zcl_report reports[CONFIG_ZZB_ZCL_REPORT_COUNT];
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
	/* A NWK unicast to the endpoint, which a Default Response may answer */
	bool unicast;
	/* The instance for the cluster and the direction, or NULL */
	const struct zzb_zcl_cluster *cluster;
};

/*
 * Sends a response to req, a ZCL frame of type with the command identifier cmd and the payload;
 * an error drops it
 */
void zzb_zcl_respond(struct zzb_node *node, const struct zzb_zcl_req *req,
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
		   uint16_t value);

/* Calls the start hooks after the records are loaded */
void zzb_zcl_start(struct zzb_node *node);

void zzb_zcl_identify_init(struct zzb_node *node);
void zzb_zcl_on_off_init(struct zzb_node *node);

/* Processes a global command; returns the status of the Default Response or ZZB_ZCL_RESPONDED */
int zzb_zcl_foundation(struct zzb_node *node, const struct zzb_zcl_req *req);

/* The attribute id of cl, or NULL */
const struct zzb_zcl_attr *zzb_zcl_attr_find(const struct zzb_zcl_cluster *cl, uint16_t id);

/* The value of an attribute of an integer type of at most 16 bits */
uint16_t zzb_zcl_attr_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr);

/* Writes the value of attr of cl in its ZCL encoding; returns its length or -ENOSPC */
int zzb_zcl_attr_encode(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint8_t *p, size_t len);

/*
 * Checks 2 to 6 of ZCL 2.5.3.3 for a remote write of the value of type at p to attr of cl. Returns
 * the status and, with ZZB_ZCL_SUCCESS, the value in *value.
 */
uint8_t zzb_zcl_attr_check(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint8_t type, const uint8_t *p, uint16_t *value);

/* Writes value to the field of attr of cl; returns whether the value changed */
bool zzb_zcl_attr_write(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint16_t value);

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
		 uint16_t value);

/*
 * Restores the defaults of the attributes through the reset hooks, which report each change, and
 * deletes the records of the non-volatile attributes
 */
void zzb_zcl_reset(struct zzb_node *node);

/* Sends the cluster-specific response id with the payload to cmd, which a cmd hook processes */
void zzb_zcl_cmd_respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t id,
			 const uint8_t *payload, size_t len);

/* Calls zcl_effect_cb */
void zzb_zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		    uint8_t variant);

/* Configure Reporting (ZCL 2.5.7); returns the Default Response status or ZZB_ZCL_RESPONDED */
int zzb_zcl_configure_reporting(struct zzb_node *node, const struct zzb_zcl_req *req);

/* Processes a frame of APSDE-DATA.indication for an endpoint other than 0, and takes buf */
void zzb_zcl_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind);

#endif /* ZZB_SUBSYS_ZCL_ZCL_H_ */
