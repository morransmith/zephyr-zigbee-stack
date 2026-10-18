/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>

#include "common/node.h"
#include "common/queue.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

#define RETRY_DELAY K_SECONDS(1)

/* The device timeout of R22 Table 3.52 in seconds */
#define TIMEOUT_S (CONFIG_ZZB_NWK_ED_TIMEOUT == 0 ? 10 : 60 * BIT(CONFIG_ZZB_NWK_ED_TIMEOUT))

/* Three keepalives per device timeout (R22 3.6.10.3) */
#define PERIOD K_MSEC(TIMEOUT_S * 1000 / 3)

static void arm(struct zzb_node *node, k_timeout_t delay)
{
	k_work_reschedule_for_queue(&zzb_workq, &node->nwk.keepalive, delay);
}

static int send_request(struct zzb_node *node, const struct zzb_nwk_neighbor *parent)
{
	struct zzb_nwk_hdr hdr = {
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE,
		.dst = parent->short_addr,
		.radius = 1,
		.dst_ext = parent->ext_addr,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_ED_TIMEOUT_REQ,
		.ed_timeout_req = {.timeout = CONFIG_ZZB_NWK_ED_TIMEOUT},
	};

	return zzb_nwk_send_cmd(node, &hdr, &cmd, parent->short_addr, true);
}

static int poll(struct zzb_node *node, const struct zzb_nwk_neighbor *parent)
{
	const struct zzb_mac_addr coord = {
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = node->nwk.nib.pan_id,
		.short_addr = parent->short_addr,
	};

	return zzb_mlme_poll_req(node, &coord);
}

static void fail(struct zzb_node *node)
{
	zzb_nwk_keepalive_stop(node);
	zzb_nwk_parent_link_failure(node);
}

static void keepalive_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, nwk.keepalive);
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	if (parent == NULL) {
		return;
	}

	if (nwk->ed_timeout_wait) {
		fail(node);
		return;
	}

	if (nwk->op != ZZB_NWK_OP_NONE) {
		arm(node, RETRY_DELAY);
		return;
	}

	if (!nwk->ed_timeout_due && (nwk->nib.parent_info & ZZB_NWK_PARENT_POLL_KEEPALIVE)) {
		arm(node, poll(node, parent) ? RETRY_DELAY : PERIOD);
		return;
	}

	if (send_request(node, parent) != 0) {
		arm(node, RETRY_DELAY);
		return;
	}

	/* Only a keepalive waits for the response (R22 3.6.10.3) */
	if (!nwk->ed_timeout_due) {
		nwk->ed_timeout_wait = true;
		arm(node, K_USEC(ZZB_MAC_RESPONSE_WAIT_US));
	}

	nwk->ed_timeout_due = false;
}

void zzb_nwk_ed_timeout_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->nwk.keepalive, keepalive_handler);
}

void zzb_nwk_ed_timeout_start(struct zzb_node *node)
{
	node->nwk.ed_timeout_due = true;
	arm(node, K_NO_WAIT);
}

void zzb_nwk_keepalive_stop(struct zzb_node *node)
{
	k_work_cancel_delayable(&node->nwk.keepalive);
	node->nwk.ed_timeout_due = false;
	node->nwk.ed_timeout_wait = false;
}

void zzb_nwk_ed_timeout_rsp_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
			       const struct zzb_nwk_cmd *cmd, uint16_t mac_src)
{
	struct zzb_nwk *nwk = &node->nwk;
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);
	uint8_t parent_info = cmd->ed_timeout_rsp.parent_info;

	if (parent == NULL || mac_src != parent->short_addr || !(hdr->flags & ZZB_NWK_SRC_IEEE) ||
	    hdr->src_ext != parent->ext_addr) {
		return;
	}

	/* Other values leave the timeout unconfigured on the parent (R22 3.6.10.2) */
	if (cmd->ed_timeout_rsp.status != 0) {
		if (nwk->ed_timeout_wait) {
			fail(node);
		}

		return;
	}

	nwk->nib.parent_info = parent_info;
	nwk->ed_timeout_wait = false;
	if (parent_info & (ZZB_NWK_PARENT_POLL_KEEPALIVE | ZZB_NWK_PARENT_ED_TIMEOUT_KEEPALIVE)) {
		arm(node, PERIOD);
	} else {
		k_work_cancel_delayable(&nwk->keepalive);
	}
}
