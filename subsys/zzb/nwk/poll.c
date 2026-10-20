/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include "common/node.h"
#include "common/queue.h"
#include "common/timer.h"
#include "mac/mac.h"
#include "nwk/nwk.h"

int zzb_nwk_poll(struct zzb_node *node, uint16_t addr)
{
	const struct zzb_mac_addr coord = {
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = node->nwk.nib.pan_id,
		.short_addr = addr,
	};

	return zzb_mlme_poll_req(node, &coord);
}

static void schedule(struct zzb_node *node)
{
	struct zzb_nwk *nwk = &node->nwk;
	int64_t now = k_uptime_get();
	bool fast = nwk->poll_holds != 0 || now < nwk->fast_poll_end;
	int64_t delay = nwk->poll_last + (fast ? nwk->short_poll_ms : nwk->long_poll_ms) - now;

	if (!node->sleepy || zzb_nwk_neighbor_parent(node) == NULL) {
		return;
	}

	zzb_timer_arm(&nwk->poll_timer, K_MSEC(MAX(delay, 0)));
}

static void poll_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, nwk.poll_timer);
	struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	if (parent == NULL) {
		return;
	}

	node->nwk.poll_last = k_uptime_get();
	/* A busy MAC runs a keepalive poll, which fetches the same frames */
	if (node->nwk.op == ZZB_NWK_OP_NONE) {
		(void)zzb_nwk_poll(node, parent->short_addr);
	}

	schedule(node);
}

void zzb_nwk_poll_init(struct zzb_node *node)
{
	struct zzb_nwk *nwk = &node->nwk;

	k_work_init_delayable(&nwk->poll_timer, poll_handler);
	nwk->long_poll_ms = CONFIG_ZZB_NWK_LONG_POLL_MS;
	nwk->short_poll_ms = CONFIG_ZZB_NWK_SHORT_POLL_MS;
}

void zzb_nwk_poll_hold(struct zzb_node *node, enum zzb_nwk_hold hold, bool on)
{
	struct zzb_nwk *nwk = &node->nwk;

	nwk->poll_holds = on ? nwk->poll_holds | hold : nwk->poll_holds & ~hold;
	schedule(node);
}

void zzb_nwk_poll_fast(struct zzb_node *node)
{
	node->nwk.fast_poll_end = k_uptime_get() + CONFIG_ZZB_NWK_FAST_POLL_MS;
	schedule(node);
}

void zzb_nwk_set_poll_intervals(struct zzb_node *node, uint32_t long_ms, uint32_t short_ms)
{
	node->nwk.long_poll_ms = long_ms;
	node->nwk.short_poll_ms = short_ms;
	schedule(node);
}

void zzb_nwk_poll_start(struct zzb_node *node)
{
	node->nwk.poll_last = k_uptime_get();
	schedule(node);
}

void zzb_nwk_poll_after(struct zzb_node *node, k_timeout_t delay)
{
	if (node->sleepy && zzb_nwk_neighbor_parent(node) != NULL) {
		zzb_timer_arm(&node->nwk.poll_timer, delay);
	}
}
