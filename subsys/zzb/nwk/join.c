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
