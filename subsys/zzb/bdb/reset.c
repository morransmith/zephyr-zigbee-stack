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

int zzb_bdb_factory_reset(struct zzb_node *node)
{
	struct zzb_bdb *bdb = &node->bdb;
	int err = bdb->on_network ? zzb_nlme_leave_req(node, false) : -ENOTCONN;

	if (err == -EBUSY) {
		return err;
	}

	/* Without a parent or a key the node cannot send the leave command */
	if (err) {
		return reset_node(node, ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
	}

	k_work_cancel_delayable(&bdb->timer);
	bdb->state = ZZB_BDB_LEAVE;
	bdb->event = ZZB_BDB_EVENT_LEFT;
	bdb->leave_rejoin = false;
	bdb->leave_status = ZZB_BDB_SUCCESS;
	return 0;
}

void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status)
{
	struct zzb_bdb *bdb = &node->bdb;

	if (bdb->state != ZZB_BDB_LEAVE) {
		return;
	}

	if (bdb->leave_rejoin) {
		zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_REJOIN);
		return;
	}

	reset_node(node, bdb->event, bdb->leave_status);
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
