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
	nwk->parent_failures = 0;
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
	nwk->leave_requested = false;
	return 0;
}

/* R22 3.6.1.10.4: with rejoin the node keeps its network, including nwkExtendedPANID */
static void leave_locally(struct zzb_node *node, bool rejoin)
{
	if (!rejoin) {
		zzb_nwk_reset(node);
		zzb_nwk_delete(node);
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
	if (nwk->leave_requested) {
		zzb_nlme_leave_ind(node, 0, nwk->leave_rejoin);
		return;
	}

	zzb_nlme_leave_conf(node, status);
}

/* R22 3.6.1.10.3.1: the node leaves as for NLME-LEAVE.request, whatever the status */
static void leave_on_request(struct zzb_node *node, const struct zzb_nwk_neighbor *parent,
			     bool rejoin)
{
	struct zzb_nwk *nwk = &node->nwk;

	if (send_leave(node, parent, rejoin) != 0) {
		leave_locally(node, rejoin);
		zzb_nlme_leave_ind(node, 0, rejoin);
		return;
	}

	nwk->op = ZZB_NWK_OP_LEAVE;
	nwk->leave_rejoin = rejoin;
	nwk->leave_requested = true;
}

/*
 * A parent that leaves without its children takes only itself off the network; the node reports
 * the lost parent, so BDB rejoins another one
 */
static void neighbor_left(struct zzb_node *node, struct zzb_nwk_neighbor *nb, bool rejoin)
{
	bool parent = nb->relationship == ZZB_NWK_REL_PARENT;
	uint16_t short_addr = nb->short_addr;
	uint64_t ext_addr = nb->ext_addr;

	zzb_nwk_neighbor_remove(nb);
	zzb_nlme_leave_ind(node, ext_addr, rejoin);
	if (parent) {
		zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE, short_addr);
	}
}

void zzb_nwk_leave_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
		      const struct zzb_nwk_cmd *cmd, uint16_t mac_src)
{
	struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);
	bool from_parent = parent != NULL && mac_src == parent->short_addr;
	bool rejoin = cmd->leave_options & ZZB_NWK_LEAVE_REJOIN;
	struct zzb_nwk_neighbor *nb;

	if (cmd->leave_options & ZZB_NWK_LEAVE_REQUEST) {
		if (from_parent && hdr->dst == node->nwk.nib.short_addr &&
		    node->nwk.op == ZZB_NWK_OP_NONE) {
			leave_on_request(node, parent, rejoin);
		}

		return;
	}

	if (from_parent && (cmd->leave_options & ZZB_NWK_LEAVE_REMOVE_CHILDREN)) {
		leave_locally(node, rejoin);
		zzb_nlme_leave_ind(node, 0, rejoin);
		return;
	}

	nb = from_parent ? parent : zzb_nwk_neighbor_find_short(node, mac_src);
	if (nb != NULL && (hdr->flags & ZZB_NWK_SRC_IEEE) && nb->ext_addr == hdr->src_ext) {
		neighbor_left(node, nb, rejoin);
	}
}
