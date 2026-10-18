/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include "common/node.h"
#include "common/queue.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

/* Network addresses from 0xfff8 are broadcast or reserved */
#define FIRST_SPECIAL_ADDR 0xfff8

static void end_rejoin(struct zzb_node *node, uint8_t status)
{
	struct zzb_nwk *nwk = &node->nwk;

	/* Back to the network of the node, since it has no parent on the candidate's */
	if (status != ZZB_NWK_STATUS_SUCCESS && nwk->candidate != NULL) {
		zzb_nlme_set_pan_id(node, nwk->prev_pan_id);
		if (nwk->nib.short_addr != nwk->prev_short_addr) {
			zzb_nlme_set_short_addr(node, nwk->prev_short_addr);
		}

		if (nwk->prev_channel != 0) {
			zzb_mlme_set_channel(node, nwk->prev_channel);
		}
	}

	k_work_cancel_delayable(&nwk->timer);
	nwk->op = ZZB_NWK_OP_NONE;
	nwk->candidate = NULL;
	zzb_nlme_join_conf(node, status);
}

/* R22 3.6.1.4.2: a random address excluding 0x0000 */
static uint16_t random_addr(void)
{
	uint16_t addr;

	do {
		addr = sys_rand16_get();
	} while (addr == 0x0000 || addr >= FIRST_SPECIAL_ADDR);

	return addr;
}

static int send_request(struct zzb_node *node, struct zzb_nwk_neighbor *nb)
{
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_hdr hdr = {
		.flags = ZZB_NWK_SRC_IEEE | (nb->ext_addr ? ZZB_NWK_DST_IEEE : 0) |
			 (nwk->join_secure ? ZZB_NWK_SECURITY : 0),
		.dst = nb->short_addr,
		.radius = 1,
		.dst_ext = nb->ext_addr,
	};
	/* Allocate address 0 under stochastic addressing */
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_REJOIN_REQ,
		.capability = nwk->nib.capability & ~ZZB_NWK_CAP_ALLOC_ADDR,
	};
	int err;

	nwk->candidate = nb;
	zzb_nlme_set_pan_id(node, nb->pan_id);
	err = zzb_mlme_set_channel(node, nb->channel);
	if (err) {
		return err;
	}

	/* The rejoin response is addressed to it */
	if (nwk->nib.short_addr >= FIRST_SPECIAL_ADDR) {
		zzb_nlme_set_short_addr(node, random_addr());
	}

	return zzb_nwk_send_cmd(node, &hdr, &cmd, nb->short_addr, true);
}

static void try_next(struct zzb_node *node)
{
	struct zzb_nwk_neighbor *nb;

	while ((nb = zzb_nwk_select_parent(node, true)) != NULL) {
		if (send_request(node, nb) == 0) {
			return;
		}

		nb->potential_parent = false;
	}

	end_rejoin(node, ZZB_NWK_STATUS_NOT_PERMITTED);
}

static void fail_candidate(struct zzb_node *node)
{
	k_work_cancel_delayable(&node->nwk.timer);
	node->nwk.candidate->potential_parent = false;
	try_next(node);
}

static void timer_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, nwk.timer);

	if (node->nwk.op == ZZB_NWK_OP_REJOIN && node->nwk.candidate != NULL) {
		fail_candidate(node);
	}
}

void zzb_nwk_rejoin_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->nwk.timer, timer_handler);
}

int zzb_nwk_rejoin_req(struct zzb_node *node, uint64_t ext_pan_id, uint32_t channels,
		       uint8_t duration, uint8_t capability, bool secure)
{
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nib *nib = &nwk->nib;
	int err;

	if (secure && !nib->keys[nib->active_key].valid) {
		return -ENOENT;
	}

	err = zzb_mlme_scan_req(node, channels ? channels : BIT(nib->channel), duration);
	if (err) {
		return err;
	}

	zzb_nwk_start_join(node, ext_pan_id, capability);
	zzb_nwk_start_scan(node);
	nwk->join_secure = secure;
	nwk->prev_pan_id = nib->pan_id;
	nwk->prev_short_addr = nib->short_addr;
	nwk->prev_channel = nib->channel;
	nwk->op = ZZB_NWK_OP_REJOIN;
	return 0;
}

void zzb_nwk_rejoin_scan_conf(struct zzb_node *node, enum zzb_mac_status status)
{
	if (status == ZZB_MAC_STATUS_NO_BEACON) {
		end_rejoin(node, ZZB_NWK_STATUS_NO_NETWORKS);
		return;
	}

	try_next(node);
}

void zzb_nwk_rejoin_req_conf(struct zzb_node *node, enum zzb_mac_status status)
{
	if (node->nwk.op != ZZB_NWK_OP_REJOIN || node->nwk.candidate == NULL) {
		return;
	}

	if (status != ZZB_MAC_STATUS_SUCCESS) {
		fail_candidate(node);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->nwk.timer, K_USEC(ZZB_MAC_RESPONSE_WAIT_US));
}

/* The response carries the IEEE address of the node as destination and a source IEEE address */
static bool ieee_addressed(const struct zzb_node *node, const struct zzb_nwk_hdr *hdr)
{
	return (hdr->flags & ZZB_NWK_DST_IEEE) && hdr->dst_ext == node->eui64 &&
	       (hdr->flags & ZZB_NWK_SRC_IEEE);
}

/* R22 3.6.1.9.3: the parent has given the node a new address */
static void update_addr(struct zzb_node *node, const struct zzb_nwk_hdr *hdr, uint16_t addr,
			uint16_t mac_src)
{
	const struct zzb_nwk_neighbor *parent = zzb_nwk_neighbor_parent(node);

	if (parent == NULL || mac_src != parent->short_addr || !ieee_addressed(node, hdr) ||
	    hdr->src_ext != parent->ext_addr || addr == node->nwk.nib.short_addr) {
		return;
	}

	zzb_nlme_set_short_addr(node, addr);
	zzb_nwk_save(node);
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_ADDR_UPDATE, addr);
}

void zzb_nwk_rejoin_rsp_rx(struct zzb_node *node, const struct zzb_nwk_hdr *hdr,
			   const struct zzb_nwk_cmd *cmd, uint16_t mac_src)
{
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_neighbor *nb = nwk->candidate;
	uint16_t addr = cmd->rejoin_rsp.short_addr;
	bool accepted =
		cmd->rejoin_rsp.status == ZZB_MAC_STATUS_SUCCESS && addr < FIRST_SPECIAL_ADDR;

	if (nwk->op != ZZB_NWK_OP_REJOIN) {
		if (accepted && nwk->op == ZZB_NWK_OP_NONE) {
			update_addr(node, hdr, addr, mac_src);
		}

		return;
	}

	if (nb == NULL || mac_src != nb->short_addr || !ieee_addressed(node, hdr) ||
	    (nb->ext_addr && hdr->src_ext != nb->ext_addr)) {
		return;
	}

	if (!accepted) {
		fail_candidate(node);
		return;
	}

	nb->ext_addr = hdr->src_ext;
	zzb_nwk_become_child(node, nb);
	if (addr != nwk->nib.short_addr) {
		zzb_nlme_set_short_addr(node, addr);
	}

	/* After a trust center rejoin the network key comes from the trust center */
	if (nwk->join_secure) {
		zzb_nwk_save(node);
		zzb_nwk_ed_timeout_start(node);
	} else {
		nwk->key_pending = true;
	}

	end_rejoin(node, ZZB_NWK_STATUS_SUCCESS);
}
