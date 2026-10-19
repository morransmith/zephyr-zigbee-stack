/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_BDB_BDB_H_
#define ZZB_SUBSYS_BDB_BDB_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zzb/bdb.h>

struct zzb_node;

/* Delay before an NLME request that returned -EBUSY is issued again */
#define ZZB_BDB_RETRY_MS 1000

/* The procedure in progress, by the callback or timer it waits for */
enum zzb_bdb_state {
	ZZB_BDB_IDLE,
	/* The NLME-JOIN of a rejoin step, or the timer that issues it */
	ZZB_BDB_REJOIN,
};

struct zzb_bdb {
	/* bdbNodeIsOnANetwork */
	bool on_network;
	enum zzb_bdb_state state;
	/* The event that ends the rejoin series */
	enum zzb_bdb_event event;
	struct k_work_delayable timer;
};

/*
 * Clears the BDB state; the node is on a network when NWK has loaded one. Called by
 * zzb_node_init() after the settings are loaded.
 */
void zzb_bdb_init(struct zzb_node *node);

/* Arms the BDB timer, which acts by the state */
void zzb_bdb_schedule(struct zzb_node *node, k_timeout_t delay);

/* Ends the procedure, then calls the event callback of the application */
void zzb_bdb_report(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status);

/* BDB 7.1 */
int zzb_bdb_initialize(struct zzb_node *node);

/* Starts the rejoin series that ends with event */
void zzb_bdb_rejoin_start(struct zzb_node *node, enum zzb_bdb_event event);
void zzb_bdb_rejoin_timer(struct zzb_node *node);
void zzb_bdb_rejoin_conf(struct zzb_node *node, uint8_t status);

#endif /* ZZB_SUBSYS_BDB_BDB_H_ */
