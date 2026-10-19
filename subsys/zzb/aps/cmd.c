/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "sec/hmac.h"

/* The source address field of a network key in a distributed security network */
#define DISTRIBUTED_TC UINT64_MAX

/* Input of the keyed hash of Verify Key (R22 4.4.10.7.4) */
#define HASH_VERIFY_KEY 0x03

/* While the trust center is known, it must be the source (BDB 10.2.2) */
static bool from_tc(const struct zzb_node *node, uint64_t addr)
{
	return node->aps.aib.tc_addr == ZZB_APS_TC_UNKNOWN || addr == node->aps.aib.tc_addr;
}

static bool is_tc(const struct zzb_node *node, uint64_t addr)
{
	return node->aps.aib.tc_addr != ZZB_APS_TC_UNKNOWN && addr == node->aps.aib.tc_addr;
}

/* R22 4.4.2.3, 4.4.1.5 */
static bool accept_nwk_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
			   const struct zzb_aps_cmd *cmd)
{
	uint64_t src = cmd->transport_key.src;
	uint64_t dst = cmd->transport_key.dst;

	if (src == DISTRIBUTED_TC || !from_tc(node, src) || (dst != node->eui64 && dst != 0)) {
		return false;
	}

	if (rx->hdr.flags & ZZB_APS_SECURITY) {
		return rx->sec.key_id == ZZB_SEC_KEY_TRANSPORT && rx->sec.src_addr == src;
	}

	return rx->nwk_security && zzb_nwk_is_broadcast(rx->dst_addr) &&
	       !zzb_nwk_key_pending(node) && is_tc(node, src);
}

/* Secured with the trust center entry and key identifier key_id */
static bool tc_key(const struct zzb_node *node, const struct zzb_aps_rx *rx,
		   enum zzb_sec_key_id key_id)
{
	return (rx->hdr.flags & ZZB_APS_SECURITY) && rx->sec.pair != NULL &&
	       is_tc(node, rx->sec.src_addr) && rx->sec.key_id == key_id;
}

static bool accept_transport_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
				 const struct zzb_aps_cmd *cmd)
{
	switch (cmd->transport_key.type) {
	case ZZB_APS_KEY_NWK:
		return accept_nwk_key(node, rx, cmd);
	case ZZB_APS_KEY_TC_LINK:
		return tc_key(node, rx, ZZB_SEC_KEY_LOAD) && !zzb_nwk_key_pending(node) &&
		       cmd->transport_key.dst == node->eui64 && is_tc(node, cmd->transport_key.src);
	default:
		return false;
	}
}

/* R22 4.4.1.3: without APS security the trust center is NWK address 0x0000 */
static bool accept_switch_key(struct zzb_node *node, const struct zzb_aps_rx *rx)
{
	if (node->aps.aib.tc_addr == ZZB_APS_TC_UNKNOWN || zzb_nwk_key_pending(node)) {
		return false;
	}

	return (rx->hdr.flags & ZZB_APS_SECURITY) ? tc_key(node, rx, ZZB_SEC_KEY_DATA)
						  : rx->src_addr == 0x0000;
}

/* R22 4.4.8.2.3 step 1, Table 4-6 */
static bool accept_confirm_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
			       const struct zzb_aps_cmd *cmd)
{
	return !zzb_nwk_is_broadcast(rx->dst_addr) && tc_key(node, rx, ZZB_SEC_KEY_DATA) &&
	       cmd->confirm_key.dst == node->eui64;
}

/* R22 4.4.8.2.3 steps 8 and 9 */
static void confirm_key(struct zzb_node *node, const struct zzb_aps_rx *rx,
			const struct zzb_aps_cmd *cmd)
{
	struct zzb_aps_key_pair *pair = rx->sec.pair;

	if (cmd->confirm_key.status == ZZB_APS_STATUS_SUCCESS &&
	    cmd->confirm_key.type == ZZB_APS_KEY_TC_LINK) {
		pair->attr = ZZB_APS_KEY_VERIFIED;
		pair->in_fc = 0;
		zzb_aps_key_save(node, pair);
	}

	zzb_apsme_confirm_key_ind(node, cmd->confirm_key.status, rx->sec.src_addr,
				  cmd->confirm_key.type);
}

bool zzb_aps_cmd_accept(struct zzb_node *node, const struct zzb_aps_rx *rx,
			const struct zzb_aps_cmd *cmd)
{
	if (cmd->id == ZZB_APS_CMD_TRANSPORT_KEY) {
		return accept_transport_key(node, rx, cmd);
	}

	if (!rx->nwk_security) {
		return false;
	}

	switch (cmd->id) {
	case ZZB_APS_CMD_SWITCH_KEY:
		return accept_switch_key(node, rx);
	case ZZB_APS_CMD_CONFIRM_KEY:
		return accept_confirm_key(node, rx, cmd);
	default:
		return false;
	}
}

void zzb_aps_cmd_process(struct zzb_node *node, const struct zzb_aps_rx *rx,
			 const struct zzb_aps_cmd *cmd)
{
	switch (cmd->id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		zzb_apsme_transport_key_ind(node, cmd->transport_key.src, cmd->transport_key.type,
					    cmd->transport_key.key, cmd->transport_key.seq);
		break;
	case ZZB_APS_CMD_SWITCH_KEY:
		zzb_apsme_switch_key_ind(node, node->aps.aib.tc_addr, cmd->switch_key_seq);
		break;
	case ZZB_APS_CMD_CONFIRM_KEY:
		confirm_key(node, rx, cmd);
		break;
	default:
		break;
	}
}

/*
 * Sends cmd to the trust center at 0x0000 (R22 4.6.2.2), with APS security under pair as data key
 * unless pair is NULL, and without ACK request (R22 4.4.10)
 */
static int send_to_tc(struct zzb_node *node, const struct zzb_aps_cmd *cmd,
		      const struct zzb_aps_key_pair *pair)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_CMD,
		.flags = pair != NULL ? ZZB_APS_SECURITY : 0,
		.counter = node->aps.counter,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	size_t hdr_len = zzb_aps_hdr_len(&hdr);
	int err = 0;

	if (buf == NULL) {
		return -ENOBUFS;
	}

	net_buf_reserve(buf, ZZB_NLDE_HEADROOM + ZZB_APS_AUX_MAX_LEN);
	zzb_aps_hdr_write(&hdr, net_buf_add(buf, hdr_len));
	net_buf_add(buf, zzb_aps_cmd_write(cmd, net_buf_tail(buf)));
	zzb_frame_meta(buf)->handle = ZZB_APS_OWN_HANDLE;

	if (pair != NULL) {
		err = zzb_aps_secure(node, buf, hdr_len, pair->key, ZZB_SEC_KEY_DATA, true);
	}

	if (err == 0) {
		err = zzb_nlde_data_req(node, buf, 0x0000, 0, true);
	}

	if (err) {
		net_buf_unref(buf);
		return err;
	}

	node->aps.counter++;
	return 0;
}

int zzb_apsme_request_key_req(struct zzb_node *node, uint64_t dst_addr)
{
	const struct zzb_aps_cmd cmd = {
		.id = ZZB_APS_CMD_REQUEST_KEY,
		.request_key.type = ZZB_APS_REQUEST_TC_LINK,
	};
	struct zzb_aps_key_pair *pair;

	if (!is_tc(node, dst_addr)) {
		return -EINVAL;
	}

	pair = zzb_aps_key_pair_find(node, dst_addr);
	if (pair == NULL || pair->attr == ZZB_APS_KEY_UNVERIFIED) {
		return -ENOENT;
	}

	return send_to_tc(node, &cmd, pair);
}

int zzb_apsme_verify_key_req(struct zzb_node *node, uint64_t dst_addr, enum zzb_aps_key_type type)
{
	struct zzb_aps_cmd cmd = {
		.id = ZZB_APS_CMD_VERIFY_KEY,
		.verify_key.type = type,
		.verify_key.src = node->eui64,
	};
	struct zzb_aps_key_pair *pair;
	int err;

	if (type != ZZB_APS_KEY_TC_LINK || !is_tc(node, dst_addr)) {
		return -EINVAL;
	}

	pair = zzb_aps_key_pair_find(node, dst_addr);
	if (pair == NULL) {
		return -ENOENT;
	}

	err = zzb_sec_hmac(pair->key, HASH_VERIFY_KEY, cmd.verify_key.hash);
	if (err) {
		return err;
	}

	return send_to_tc(node, &cmd, NULL);
}
