/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_BDB_BDB_H_
#define ZZB_SUBSYS_BDB_BDB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zzb/bdb.h>

struct zzb_node;
struct zzb_nwk_network;

/* Delay before an NLME request that returned -EBUSY is issued again */
#define ZZB_BDB_RETRY_MS 1000

/* bdbScanDuration */
#define ZZB_BDB_SCAN_DURATION 3

/* Channels 11 to 26 of channel page 0 */
#define ZZB_BDB_ALL_CHANNELS 0x07fff800

/* apsSecurityTimeOutPeriod of the Zigbee PRO stack profile (PICS 08-0006-05 Table 4) */
#define ZZB_BDB_KEY_WAIT_MS 1700

/* Pause before the next rejoin series: doubled after each failed series up to the maximum */
#define ZZB_BDB_PAUSE_MIN_MS (30 * MSEC_PER_SEC)
#define ZZB_BDB_PAUSE_MAX_MS (15 * 60 * MSEC_PER_SEC)

/* The procedure in progress, by the callback or timer it waits for */
enum zzb_bdb_state {
	ZZB_BDB_IDLE,
	/* The NLME-JOIN of a rejoin step, or the timer that issues it */
	ZZB_BDB_REJOIN,
	/* The network key after a trust center rejoin */
	ZZB_BDB_REJOIN_KEY,
	/* The pause before the next rejoin series */
	ZZB_BDB_REJOIN_PAUSE,
	/* Network steering: the discovery, the association and the network key */
	ZZB_BDB_STEER_DISCOVERY,
	ZZB_BDB_STEER_JOIN,
	ZZB_BDB_STEER_KEY,
	/* NLME-LEAVE.confirm */
	ZZB_BDB_LEAVE,
	/* The trust center link key exchange: Node_Desc_rsp, the new key, Confirm Key */
	ZZB_BDB_TCLK_NODE_DESC,
	ZZB_BDB_TCLK_REQUEST_KEY,
	ZZB_BDB_TCLK_VERIFY_KEY,
};

struct zzb_bdb {
	/* bdbNodeIsOnANetwork */
	bool on_network;
	enum zzb_bdb_state state;
	/* The event that ends the rejoin series */
	enum zzb_bdb_event event;
	/* Rejoin: the step of the series, the next pause, and whether NO_NETWORK was reported */
	uint8_t step;
	uint32_t pause_ms;
	bool reported;
	/* Steering: vDoPrimaryScan FALSE, the networks of the discovery and the next one to try */
	bool secondary;
	const struct zzb_nwk_network *networks;
	size_t network_count;
	size_t next_network;
	/* Steering: the network joined, and the networks whose key did not arrive */
	uint64_t ext_pan_id;
	uint64_t failed[CONFIG_ZZB_NWK_NETWORK_COUNT];
	size_t failed_count;
	/* Leave: the rejoin flag, and the status of the event after the reset */
	bool leave_rejoin;
	enum zzb_bdb_status leave_status;
	/* bdbTCLinkKeyExchangeAttempts */
	uint8_t attempts;
	struct k_work_delayable timer;
};

/*
 * Clears the BDB state; the node is on a network when NWK has loaded one. Called by
 * zzb_node_init() after the settings are loaded.
 */
void zzb_bdb_init(struct zzb_node *node);

/* Arms the BDB timer, which acts by the state */
void zzb_bdb_schedule(struct zzb_node *node, k_timeout_t delay);

/* Calls the event callback of the application */
void zzb_bdb_notify(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status);

/* Ends the procedure, then calls the event callback */
void zzb_bdb_report(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status);

/* BDB 7.1 */
int zzb_bdb_initialize(struct zzb_node *node);

/* Starts the rejoin series that ends with event */
void zzb_bdb_rejoin_start(struct zzb_node *node, enum zzb_bdb_event event);
void zzb_bdb_rejoin_timer(struct zzb_node *node);
void zzb_bdb_rejoin_conf(struct zzb_node *node, uint8_t status);

/* Called by ZDO after NLME-SET of a network key from a Transport Key */
void zzb_bdb_nwk_key_ind(struct zzb_node *node);

/* BDB 8.1, 8.2 */
int zzb_bdb_steer(struct zzb_node *node);
void zzb_bdb_steering_timer(struct zzb_node *node);
void zzb_bdb_steering_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count);
void zzb_bdb_steering_join_conf(struct zzb_node *node, uint8_t status);
void zzb_bdb_steering_key_ind(struct zzb_node *node);

/* BDB 8.2 steps 14 to 16, after the trust center link key exchange */
void zzb_bdb_steering_done(struct zzb_node *node);

/* BDB 9.5 */
int zzb_bdb_factory_reset(struct zzb_node *node);

/* BDB 9.4: called by ZDO after the response to Mgmt_Leave_req with its rejoin flag */
void zzb_bdb_leave(struct zzb_node *node, bool rejoin);

/* Leaves without rejoin; the reset that follows reports event with status */
void zzb_bdb_leave_and_reset(struct zzb_node *node, enum zzb_bdb_event event,
			     enum zzb_bdb_status status);
void zzb_bdb_leave_timer(struct zzb_node *node);

/* BDB 10.2.5 */
void zzb_bdb_tclk_start(struct zzb_node *node);
void zzb_bdb_tclk_timer(struct zzb_node *node);

/* Called by ZDO for a Transport Key with a trust center link key of ZZB_SEC_KEY_LEN bytes */
void zzb_bdb_tc_link_key_ind(struct zzb_node *node, uint64_t src_addr, const uint8_t *key);

#endif /* ZZB_SUBSYS_BDB_BDB_H_ */
