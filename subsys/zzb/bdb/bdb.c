/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"

static void timer_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, bdb.timer);

	switch (node->bdb.state) {
	case ZZB_BDB_REJOIN:
	case ZZB_BDB_REJOIN_KEY:
	case ZZB_BDB_REJOIN_PAUSE:
		zzb_bdb_rejoin_timer(node);
		break;
	default:
		break;
	}
}

void zzb_bdb_init(struct zzb_node *node)
{
	/* Safe on the zeroed struct of a new node */
	k_work_cancel_delayable(&node->bdb.timer);
	node->bdb = (struct zzb_bdb){
		.on_network = zzb_nlme_get_ext_pan_id(node) != 0,
	};
	k_work_init_delayable(&node->bdb.timer, timer_handler);
}

void zzb_bdb_schedule(struct zzb_node *node, k_timeout_t delay)
{
	k_work_reschedule_for_queue(&zzb_workq, &node->bdb.timer, delay);
}

void zzb_bdb_notify(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	if (node->bdb_cb != NULL) {
		node->bdb_cb(node, event, status);
	}
}

/* The callback may start the next procedure, so nothing follows it */
void zzb_bdb_report(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	node->bdb.state = ZZB_BDB_IDLE;
	k_work_cancel_delayable(&node->bdb.timer);
	zzb_bdb_notify(node, event, status);
}

void zzb_nlme_join_conf(struct zzb_node *node, uint8_t status)
{
	if (node->bdb.state == ZZB_BDB_REJOIN) {
		zzb_bdb_rejoin_conf(node, status);
	}
}

struct call {
	struct k_work work;
	struct zzb_node *node;
	int (*fn)(struct zzb_node *node);
	int err;
};

static void call_handler(struct k_work *work)
{
	struct call *call = CONTAINER_OF(work, struct call, work);

	call->err = call->fn(call->node);
}

/* Runs fn on the stack thread */
static int call(struct zzb_node *node, int (*fn)(struct zzb_node *node))
{
	struct call call = {
		.node = node,
		.fn = fn,
	};

	zzb_call_sync(&call.work, call_handler);
	return call.err;
}

int zzb_bdb_start(struct zzb_node *node)
{
	return call(node, zzb_bdb_initialize);
}
