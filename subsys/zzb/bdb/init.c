/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

int zzb_bdb_initialize(struct zzb_node *node)
{
	if (node->bdb.state != ZZB_BDB_IDLE) {
		return -EBUSY;
	}

	if (!node->bdb.on_network) {
		zzb_bdb_report(node, ZZB_BDB_EVENT_INIT, ZZB_BDB_NOT_ON_A_NETWORK);
		return 0;
	}

	zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_INIT);
	return 0;
}

/* The NWK callback that reports the cause may run inside a confirm, so the timer issues the step */
void zzb_bdb_rejoin_start(struct zzb_node *node, enum zzb_bdb_event event)
{
	node->bdb.state = ZZB_BDB_REJOIN;
	node->bdb.event = event;
	zzb_bdb_schedule(node, K_NO_WAIT);
}

/* BDB 7.1 step 4 */
void zzb_bdb_rejoin_timer(struct zzb_node *node)
{
	int err = zzb_nlme_join_req(node, zzb_nlme_get_ext_pan_id(node), ZZB_NWK_JOIN_REJOIN, 0, 0,
				    zzb_nlme_get_capability(node), true);

	if (err == -EBUSY) {
		zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_RETRY_MS));
		return;
	}

	if (err) {
		zzb_bdb_rejoin_conf(node, ZZB_NWK_STATUS_NOT_PERMITTED);
	}
}

void zzb_bdb_rejoin_conf(struct zzb_node *node, uint8_t status)
{
	if (status != ZZB_NWK_STATUS_SUCCESS) {
		zzb_bdb_report(node, node->bdb.event, ZZB_BDB_NO_NETWORK);
		return;
	}

	zzb_zdo_device_annce_req(node);
	zzb_bdb_report(node, node->bdb.event, ZZB_BDB_SUCCESS);
}
