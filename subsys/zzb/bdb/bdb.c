/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zzb/bdb.h>
#include <zzb/zzb.h>

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
	case ZZB_BDB_STEER_DISCOVERY:
	case ZZB_BDB_STEER_JOIN:
	case ZZB_BDB_STEER_KEY:
		zzb_bdb_steering_timer(node);
		break;
	case ZZB_BDB_LEAVE:
		zzb_bdb_leave_timer(node);
		break;
	case ZZB_BDB_TCLK_NODE_DESC:
	case ZZB_BDB_TCLK_REQUEST_KEY:
	case ZZB_BDB_TCLK_VERIFY_KEY:
		zzb_bdb_tclk_timer(node);
		break;
	default:
		break;
	}
}

void zzb_bdb_init(struct zzb_node *node)
{
	/* Safe on the zeroed struct of a new node */
	k_work_cancel_delayable(&node->bdb.timer);
	k_work_cancel_delayable(&node->bdb.fb.timer);
	node->bdb = (struct zzb_bdb){
		.on_network = zzb_nlme_get_ext_pan_id(node) != 0,
	};
	k_work_init_delayable(&node->bdb.timer, timer_handler);
	zzb_bdb_fb_init(node);
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

uint8_t zzb_bdb_capability(const struct zzb_node *node)
{
	uint8_t cap = ZZB_NWK_CAP_ALLOC_ADDR;

	if (!node->sleepy) {
		cap |= ZZB_NWK_CAP_RX_ON_IDLE;
	}

	if (node->device != NULL && (node->device->power_source & ZZB_POWER_MAINS)) {
		cap |= ZZB_NWK_CAP_MAINS;
	}

	return cap;
}

void zzb_nlme_network_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count)
{
	if (node->bdb.state == ZZB_BDB_STEER_DISCOVERY) {
		zzb_bdb_steering_discovery_conf(node, status, networks, count);
	}
}

void zzb_nlme_join_conf(struct zzb_node *node, uint8_t status)
{
	switch (node->bdb.state) {
	case ZZB_BDB_REJOIN:
		zzb_bdb_rejoin_conf(node, status);
		break;
	case ZZB_BDB_STEER_JOIN:
		zzb_bdb_steering_join_conf(node, status);
		break;
	default:
		break;
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

int zzb_bdb_steering(struct zzb_node *node)
{
	return call(node, zzb_bdb_steer);
}

int zzb_bdb_reset(struct zzb_node *node)
{
	return call(node, zzb_bdb_factory_reset);
}
