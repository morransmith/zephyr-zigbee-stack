/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>

#include "common/node.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

void zzb_nwk_reset(struct zzb_node *node)
{
	struct zzb_nwk *nwk = &node->nwk;
	uint8_t seq = nwk->nib.seq;

	zzb_nwk_keepalive_stop(node);
	k_work_cancel_delayable(&nwk->timer);
	nwk->nib = (struct zzb_nib){
		.seq = seq,
	};
	zzb_nlme_set_pan_id(node, 0xffff);
	zzb_nlme_set_short_addr(node, 0xffff);

	ARRAY_FOR_EACH_PTR(nwk->neighbors, nb) {
		zzb_nwk_neighbor_remove(nb);
	}

	nwk->network_count = 0;
	nwk->candidate = NULL;
	nwk->key_pending = false;
}

int zzb_nlme_reset_req(struct zzb_node *node)
{
	if (node->nwk.op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	zzb_nwk_reset(node);
	return 0;
}

/* R22 3.6.1.10.1: to 0xfffd, through the parent */
static int send_leave(struct zzb_node *node, const struct zzb_nwk_neighbor *parent, bool rejoin)
{
	struct zzb_nwk_hdr hdr = {
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE,
		.dst = 0xfffd,
		.radius = 1,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_LEAVE,
		.leave_options = rejoin ? ZZB_NWK_LEAVE_REJOIN : 0,
	};

	return zzb_nwk_send_cmd(node, &hdr, &cmd, parent->short_addr, true);
}

int zzb_nlme_leave_req(struct zzb_node *node, bool rejoin)
{
	struct zzb_nwk *nwk = &node->nwk;
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);
	int err;

	if (nwk->op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	if (nwk->nib.ext_pan_id == 0 || parent == NULL) {
		return -ENOTCONN;
	}

	err = send_leave(node, parent, rejoin);
	if (err) {
		return err;
	}

	nwk->op = ZZB_NWK_OP_LEAVE;
	nwk->leave_rejoin = rejoin;
	return 0;
}

/* R22 3.6.1.10.4: with rejoin the node keeps its network, including nwkExtendedPANID */
static void leave_locally(struct zzb_node *node, bool rejoin)
{
	if (!rejoin) {
		zzb_nwk_reset(node);
	}
}

void zzb_nwk_leave_conf(struct zzb_node *node, enum zzb_mac_status status)
{
	struct zzb_nwk *nwk = &node->nwk;

	if (nwk->op != ZZB_NWK_OP_LEAVE) {
		return;
	}

	nwk->op = ZZB_NWK_OP_NONE;
	leave_locally(node, nwk->leave_rejoin);
	zzb_nlme_leave_conf(node, status);
}
