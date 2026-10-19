/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "nwk/nwk.h"

/* The time a sender with the same constants retransmits; R22 does not define it */
#define DUP_LIFETIME_MS (ZZB_APS_ACK_WAIT_MS * (ZZB_APS_MAX_FRAME_RETRIES + 1))

bool zzb_aps_dup_check(struct zzb_node *node, uint16_t src_addr, uint8_t counter)
{
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->aps.dups, dup) {
		if (dup->expiry > now && dup->src_addr == src_addr && dup->counter == counter) {
			return true;
		}
	}

	return false;
}

void zzb_aps_dup_add(struct zzb_node *node, uint16_t src_addr, uint8_t counter)
{
	struct zzb_aps_dup *oldest = &node->aps.dups[0];

	ARRAY_FOR_EACH_PTR(node->aps.dups, dup) {
		if (dup->expiry < oldest->expiry) {
			oldest = dup;
		}
	}

	*oldest = (struct zzb_aps_dup){
		.src_addr = src_addr,
		.counter = counter,
		.expiry = k_uptime_get() + DUP_LIFETIME_MS,
	};
}

struct net_buf *zzb_aps_ack_build(struct zzb_node *node, const struct zzb_aps_hdr *hdr,
				  const struct zzb_aps_sec *sec)
{
	const struct zzb_aps_hdr ack = {
		.type = ZZB_APS_FRAME_ACK,
		.flags = (hdr->type == ZZB_APS_FRAME_CMD ? ZZB_APS_ACK_FORMAT : 0) |
			 (hdr->flags & (ZZB_APS_SECURITY | ZZB_APS_EXT_HDR)),
		.dst_ep = hdr->src_ep,
		.cluster = hdr->cluster,
		.profile = hdr->profile,
		.src_ep = hdr->dst_ep,
		.counter = hdr->counter,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	size_t hdr_len = zzb_aps_hdr_len(&ack);

	if (buf == NULL) {
		return NULL;
	}

	net_buf_reserve(buf, ZZB_NLDE_HEADROOM + ZZB_APS_AUX_MAX_LEN);
	zzb_aps_hdr_write(&ack, net_buf_add(buf, hdr_len));
	zzb_frame_meta(buf)->handle = ZZB_APS_OWN_HANDLE;

	/* The extended nonce lets a receiver without nwkAddressMap find the key */
	if ((ack.flags & ZZB_APS_SECURITY) &&
	    zzb_aps_secure(node, buf, hdr_len, sec->key, ZZB_SEC_KEY_DATA, true)) {
		net_buf_unref(buf);
		return NULL;
	}

	return buf;
}

void zzb_aps_send_own(struct zzb_node *node, struct net_buf *buf, uint16_t dst)
{
	if (zzb_nlde_data_req(node, buf, dst, 0, true)) {
		net_buf_unref(buf);
	}
}
