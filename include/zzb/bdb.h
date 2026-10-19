/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_BDB_H_
#define ZZB_INCLUDE_ZZB_BDB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zzb_node;

/** Status of a BDB procedure, the values of bdbCommissioningStatus that BDB uses (BDB Table 4). */
enum zzb_bdb_status {
	ZZB_BDB_SUCCESS,
	ZZB_BDB_NO_NETWORK,
	ZZB_BDB_TCLK_EX_FAILURE,
	ZZB_BDB_NOT_ON_A_NETWORK,
};

/** The procedure that an event ends. */
enum zzb_bdb_event {
	/** The initialization of zzb_bdb_start(). */
	ZZB_BDB_EVENT_INIT,
	/** The network steering of zzb_bdb_steering(). */
	ZZB_BDB_EVENT_STEERING,
	/** A rejoin after the loss of the parent, an address conflict or a leave with rejoin. */
	ZZB_BDB_EVENT_REJOIN,
	/** The node has left its network and is reset. */
	ZZB_BDB_EVENT_LEFT,
	/** Finding & binding for a target endpoint of zzb_bdb_finding_binding_target(). */
	ZZB_BDB_EVENT_FINDING_BINDING_TARGET,
};

/**
 * BDB event callback, called on the stack thread. It may call the BDB functions. A rejoin reports
 * ZZB_BDB_NO_NETWORK after its first failure and ZZB_BDB_SUCCESS when a later attempt succeeds, so
 * ZZB_BDB_EVENT_INIT and ZZB_BDB_EVENT_REJOIN can come twice.
 */
typedef void (*zzb_bdb_cb_t)(struct zzb_node *node, enum zzb_bdb_event event,
			     enum zzb_bdb_status status);

/**
 * @brief Start the initialization procedure (BDB 7.1).
 *
 * A node on a network rejoins it and ends with ZZB_BDB_EVENT_INIT; a node off a network reports
 * ZZB_BDB_NOT_ON_A_NETWORK. The event may come before the call returns.
 *
 * @retval 0 on success.
 * @retval -EBUSY a BDB procedure runs.
 */
int zzb_bdb_start(struct zzb_node *node);

/**
 * @brief Start network steering (BDB 8.1, 8.2).
 *
 * A node off a network scans bdbPrimaryChannelSet, then bdbSecondaryChannelSet, joins an open
 * network and receives the network key. The procedure ends with ZZB_BDB_EVENT_STEERING, which may
 * come before the call returns.
 *
 * @retval 0 on success.
 * @retval -EBUSY a BDB procedure runs.
 */
int zzb_bdb_steering(struct zzb_node *node);

/**
 * @brief Reset the node to its factory settings (BDB 9.5).
 *
 * A node on a network leaves it first. The node clears its network, keys and bindings, keeps its
 * outgoing frame counters and reports ZZB_BDB_EVENT_LEFT, which may come before the call returns.
 * The reset cancels the pause between rejoin attempts.
 *
 * @retval 0 on success.
 * @retval -EBUSY the network layer runs a procedure.
 */
int zzb_bdb_reset(struct zzb_node *node);

/**
 * @brief Start finding & binding for a target endpoint (BDB 8.4).
 *
 * The Identify server on ep identifies for bdbcMinCommissioningTime (180 s), and an initiator can
 * find and bind it. ZZB_BDB_EVENT_FINDING_BINDING_TARGET ends the procedure when IdentifyTime
 * becomes 0, or reports ZZB_BDB_NOT_ON_A_NETWORK at once on a node off a network. The procedure
 * runs beside the other BDB procedures; the reset of the node ends it without an event.
 *
 * @retval 0 on success.
 * @retval -ENOENT ep has no Identify server.
 * @retval -EBUSY finding & binding for a target runs.
 */
int zzb_bdb_finding_binding_target(struct zzb_node *node, uint8_t ep);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_BDB_H_ */
