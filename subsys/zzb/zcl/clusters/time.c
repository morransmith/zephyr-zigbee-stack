/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zzb/zcl/time.h>

#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"

/* Time and UTCTime: not a value (ZCL 2.6.2.24) */
#define NO_TIME 0xffffffff

void zzb_zcl_clock_set(struct zzb_node *node, uint32_t utc, int32_t offset)
{
	node->zcl.clock = (struct zzb_zcl_clock){
		.set = true,
		.utc = utc,
		.uptime = k_uptime_get(),
		.offset = offset,
	};
}

uint32_t zzb_zcl_clock_utc(const struct zzb_node *node)
{
	const struct zzb_zcl_clock *c = &node->zcl.clock;

	if (!c->set) {
		return NO_TIME;
	}

	return c->utc + (k_uptime_get() - c->uptime) / MSEC_PER_SEC;
}

uint32_t zzb_zcl_clock_local(const struct zzb_node *node)
{
	const struct zzb_zcl_clock *c = &node->zcl.clock;

	return c->set ? zzb_zcl_clock_utc(node) + c->offset : NO_TIME;
}

struct time_req {
	struct k_work work;
	struct zzb_node *node;
	uint32_t utc;
	uint32_t local;
};

static void time_handler(struct k_work *work)
{
	struct time_req *req = CONTAINER_OF(work, struct time_req, work);

	req->utc = zzb_zcl_clock_utc(req->node);
	req->local = zzb_zcl_clock_local(req->node);
}

int zzb_zcl_time_get(struct zzb_node *node, uint32_t *utc, uint32_t *local)
{
	struct time_req req = {
		.node = node,
	};

	zzb_call_sync(&req.work, time_handler);
	if (req.utc == NO_TIME) {
		return -ENODATA;
	}

	*utc = req.utc;
	*local = req.local;
	return 0;
}
