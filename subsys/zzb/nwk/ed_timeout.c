/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include "common/node.h"
#include "common/queue.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

#define RETRY_DELAY K_SECONDS(1)

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

static void keepalive_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, nwk.keepalive);
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	if (!nwk->ed_timeout_due || parent == NULL) {
		return;
	}

	if (send_request(node, parent) != 0) {
		k_work_reschedule_for_queue(&zzb_workq, &nwk->keepalive, RETRY_DELAY);
		return;
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
	k_work_reschedule_for_queue(&zzb_workq, &node->nwk.keepalive, K_NO_WAIT);
}

void zzb_nwk_ed_timeout_rsp_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
			       const struct zzb_nwk_cmd *cmd, uint16_t mac_src)
{
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	if (parent == NULL || mac_src != parent->short_addr || !(hdr->flags & ZZB_NWK_SRC_IEEE) ||
	    hdr->src_ext != parent->ext_addr) {
		return;
	}

	/* Other values leave the timeout unconfigured on the parent (R22 3.6.10.2) */
	if (cmd->ed_timeout_rsp.status == 0) {
		node->nwk.nib.parent_info = cmd->ed_timeout_rsp.parent_info;
	}
}
