/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>

#include "common/buf.h"
#include "common/node.h"
#include "mac/frame.h"
#include "mac/mac.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

#define STACK_PROFILE_PRO 2

/* R22 3.6.1.4.1: every scan makes the remaining entries potential parents again */
static void start_scan(struct zzb_node *node)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->relationship == ZZB_NWK_REL_NONE) {
			zzb_nwk_neighbor_remove(nb);
		}

		nb->potential_parent = true;
	}

	node->nwk.network_count = 0;
}

int zzb_nlme_network_discovery_req(struct zzb_node *node, uint32_t channels, uint8_t duration)
{
	int err;

	if (node->nwk.op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	err = zzb_mlme_scan_req(node, channels, duration);
	if (err) {
		return err;
	}

	start_scan(node);
	node->nwk.op = ZZB_NWK_OP_DISCOVERY;
	return 0;
}

static void add_network(struct zzb_nwk *nwk, const struct zzb_nwk_beacon *beacon, uint16_t pan_id,
			uint8_t channel, bool permit_join)
{
	struct zzb_nwk_network *net = NULL;

	for (size_t i = 0; i < nwk->network_count; i++) {
		if (nwk->networks[i].ext_pan_id == beacon->ext_pan_id) {
			net = &nwk->networks[i];
		}
	}

	if (net == NULL) {
		if (nwk->network_count == ARRAY_SIZE(nwk->networks)) {
			return;
		}

		net = &nwk->networks[nwk->network_count++];
		*net = (struct zzb_nwk_network){
			.ext_pan_id = beacon->ext_pan_id,
			.pan_id = pan_id,
			.channel = channel,
			.update_id = beacon->update_id,
		};
	}

	net->permit_join |= permit_join;
	net->router_capacity |= beacon->router_capacity;
	net->ed_capacity |= beacon->ed_capacity;
}

static struct zzb_nwk_neighbor *find_router(struct zzb_node *node, uint64_t ext_pan_id,
					    uint16_t short_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use && nb->beacon.ext_pan_id == ext_pan_id &&
		    nb->short_addr == short_addr) {
			return nb;
		}
	}

	return NULL;
}

static void add_router(struct zzb_node *node, const struct zzb_nwk_beacon *beacon,
		       const struct zzb_frame_meta *meta, uint8_t channel, uint16_t superframe)
{
	struct zzb_nwk_neighbor *nb = find_router(node, beacon->ext_pan_id, meta->src.short_addr);

	if (nb == NULL) {
		nb = zzb_nwk_neighbor_add(node, 0, meta->src.short_addr, ZZB_NWK_REL_NONE);
		if (nb == NULL) {
			return;
		}

		nb->potential_parent = true;
	}

	nb->device_type = (superframe & ZZB_MAC_SF_PAN_COORD) ? ZZB_NWK_DEVICE_COORDINATOR
							      : ZZB_NWK_DEVICE_ROUTER;
	nb->lqi = meta->lqi;
	nb->beacon = *beacon;
	nb->pan_id = meta->src.pan_id;
	nb->channel = channel;
	nb->permit_join = superframe & ZZB_MAC_SF_ASSOC_PERMIT;
}

void zzb_mlme_beacon_notify_ind(struct zzb_node *node, struct net_buf *buf, uint8_t channel,
				uint16_t superframe)
{
	const struct zzb_frame_meta meta = *zzb_frame_meta(buf);
	bool permit_join = superframe & ZZB_MAC_SF_ASSOC_PERMIT;
	struct zzb_nwk_beacon beacon;
	int len = zzb_nwk_beacon_parse(buf->data, buf->len, &beacon);

	net_buf_unref(buf);
	if (node->nwk.op != ZZB_NWK_OP_DISCOVERY || len < 0 ||
	    beacon.stack_profile != STACK_PROFILE_PRO ||
	    beacon.protocol_version != ZZB_NWK_PROTOCOL_VERSION ||
	    meta.src.mode != ZZB_MAC_ADDR_SHORT) {
		return;
	}

	add_network(&node->nwk, &beacon, meta.src.pan_id, channel, permit_join);

	/* Routers that do not permit association cannot be joined, so they do not fill the table */
	if (permit_join) {
		add_router(node, &beacon, &meta, channel, superframe);
	}
}

void zzb_mlme_scan_conf(struct zzb_node *node, enum zzb_mac_status status)
{
	struct zzb_nwk *nwk = &node->nwk;

	if (nwk->op != ZZB_NWK_OP_DISCOVERY) {
		return;
	}

	nwk->op = ZZB_NWK_OP_NONE;
	zzb_nlme_network_discovery_conf(node, status, nwk->networks, nwk->network_count);
}

static bool suitable(const struct zzb_nwk_neighbor *nb, uint64_t ext_pan_id)
{
	return nb->in_use && nb->potential_parent && nb->beacon.ext_pan_id == ext_pan_id &&
	       nb->beacon.ed_capacity && nb->permit_join;
}

/* The highest LQI wins; link cost and depth are not used (doc/architecture.md, section 10) */
static struct zzb_nwk_neighbor *select_parent(struct zzb_node *node, uint64_t ext_pan_id)
{
	struct zzb_nwk_neighbor *best = NULL;

	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (suitable(nb, ext_pan_id) && (best == NULL || nb->lqi > best->lqi)) {
			best = nb;
		}
	}

	return best;
}

static int associate(struct zzb_node *node, struct zzb_nwk_neighbor *nb)
{
	const struct zzb_mac_addr coord = {
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = nb->pan_id,
		.short_addr = nb->short_addr,
	};

	node->nwk.candidate = nb;
	return zzb_mlme_assoc_req(node, nb->channel, &coord, node->nwk.nib.capability);
}

int zzb_nlme_join_req(struct zzb_node *node, uint64_t ext_pan_id, enum zzb_nwk_join_method method,
		      uint32_t channels, uint8_t duration, uint8_t capability, bool secure)
{
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_neighbor *nb;
	int err;

	if (ext_pan_id == 0) {
		return -EINVAL;
	}

	/* Routers come with milestone 15, and polling for the Transport Key with milestone 14 */
	if (method != ZZB_NWK_JOIN_ASSOC || (capability & ZZB_NWK_CAP_ROUTER) ||
	    !(capability & ZZB_NWK_CAP_RX_ON_IDLE)) {
		return -ENOTSUP;
	}

	if (nwk->op != ZZB_NWK_OP_NONE) {
		return -EBUSY;
	}

	if (nwk->nib.ext_pan_id != 0) {
		return -EALREADY;
	}

	nb = select_parent(node, ext_pan_id);
	if (nb == NULL) {
		return -ENETUNREACH;
	}

	nwk->nib.capability = capability;
	nwk->join_ext_pan_id = ext_pan_id;
	err = associate(node, nb);
	if (err) {
		return err;
	}

	nwk->op = ZZB_NWK_OP_JOIN;
	return 0;
}

static void end_join(struct zzb_node *node, uint8_t status)
{
	node->nwk.op = ZZB_NWK_OP_NONE;
	node->nwk.candidate = NULL;
	zzb_nlme_join_conf(node, status);
}

/* The parent becomes the only entry; the NIB takes its network */
static void become_child(struct zzb_node *node, struct zzb_nwk_neighbor *parent)
{
	struct zzb_nib *nib = &node->nwk.nib;

	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb != parent) {
			zzb_nwk_neighbor_remove(nb);
		}
	}

	parent->relationship = ZZB_NWK_REL_PARENT;
	nib->ext_pan_id = parent->beacon.ext_pan_id;
	nib->pan_id = parent->pan_id;
	nib->update_id = parent->beacon.update_id;
	nib->channel = parent->channel;
}

void zzb_mlme_assoc_conf(struct zzb_node *node, uint16_t short_addr, enum zzb_mac_status status)
{
	struct zzb_nwk *nwk = &node->nwk;
	struct zzb_nwk_neighbor *nb = nwk->candidate;

	if (nwk->op != ZZB_NWK_OP_JOIN) {
		return;
	}

	if (status != ZZB_MAC_STATUS_SUCCESS) {
		nb->potential_parent = false;
		nb = select_parent(node, nwk->join_ext_pan_id);
		if (nb == NULL || associate(node, nb) != 0) {
			end_join(node, status);
		}

		return;
	}

	/* The MAC has set macPANId and macShortAddress */
	nb->ext_addr = zzb_mlme_get_coord_ext_addr(node);
	become_child(node, nb);
	nwk->nib.short_addr = short_addr;
	nwk->key_pending = true;
	end_join(node, ZZB_MAC_STATUS_SUCCESS);
}
