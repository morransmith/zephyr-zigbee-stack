/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/*
 * BDB 9: clears NWK, APS and ZDO and keeps the outgoing frame counters, then reports event. Returns
 * -EBUSY and changes nothing while NWK runs a procedure.
 */
static int reset_node(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	int err = zzb_nlme_reset_req(node);

	if (err) {
		return err;
	}

	zzb_aps_reset(node);
	zzb_zdo_init(node);
	node->bdb.on_network = false;
	zzb_bdb_report(node, event, status);
	return 0;
}

/* The node has left, or cannot send the leave command: a rejoin follows, or the reset */
static void left(struct zzb_node *node)
{
	struct zzb_bdb *bdb = &node->bdb;

	if (bdb->leave_rejoin) {
		zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_REJOIN);
		return;
	}

	reset_node(node, bdb->event, bdb->leave_status);
}

/* The leave replaces the procedure in progress */
static void start_leave(struct zzb_node *node, bool rejoin, enum zzb_bdb_event event,
			enum zzb_bdb_status status)
{
	struct zzb_bdb *bdb = &node->bdb;

	k_work_cancel_delayable(&bdb->timer);
	bdb->state = ZZB_BDB_LEAVE;
	bdb->event = event;
	bdb->leave_rejoin = rejoin;
	bdb->leave_status = status;
}

int zzb_bdb_factory_reset(struct zzb_node *node)
{
	int err = node->bdb.on_network ? zzb_nlme_leave_req(node, false) : -ENOTCONN;

	if (err == -EBUSY) {
		return err;
	}

	/* Without a parent or a key the node cannot send the leave command */
	if (err) {
		return reset_node(node, ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	}

	start_leave(node, false, ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	return 0;
}

static void leave(struct zzb_node *node)
{
	int err = zzb_nlme_leave_req(node, node->bdb.leave_rejoin);

	if (err == -EBUSY) {
		zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_RETRY_MS));
		return;
	}

	if (err) {
		left(node);
	}
}

void zzb_bdb_leave(struct zzb_node *node, bool rejoin)
{
	start_leave(node, rejoin, ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	leave(node);
}

void zzb_bdb_leave_and_reset(struct zzb_node *node, enum zzb_bdb_event event,
			     enum zzb_bdb_status status)
{
	start_leave(node, false, event, status);
	leave(node);
}

void zzb_bdb_leave_timer(struct zzb_node *node)
{
	leave(node);
}

void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status)
{
	if (node->bdb.state == ZZB_BDB_LEAVE) {
		left(node);
	}
}

/* BDB 9.3; NWK reports a lost parent with parent link failure */
void zzb_nlme_leave_ind(struct zzb_node *node, uint64_t ext_addr, bool rejoin)
{
	if (ext_addr != 0) {
		return;
	}

	if (rejoin && node->bdb.on_network) {
		zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_REJOIN);
		return;
	}

	reset_node(node, ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}
