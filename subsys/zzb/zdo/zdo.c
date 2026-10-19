/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

void zzb_zdo_init(struct zzb_node *node)
{
	node->zdo = (struct zzb_zdo){
		.tsn = sys_rand8_get(),
	};
}

int zzb_zdo_send(struct zzb_node *node, uint16_t dst, uint16_t cluster, uint8_t tsn,
		 const uint8_t *payload, size_t len, uint8_t handle)
{
	/* Endpoints and profile 0 address ZDO (R22 2.5.3) */
	const struct zzb_apsde_req req = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = dst,
		.cluster = cluster,
		.tx_options = zzb_nwk_is_broadcast(dst) ? 0 : ZZB_APS_TX_ACK,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	int err;

	if (buf == NULL) {
		return -ENOBUFS;
	}

	/* ZDO frames have no APS security, so the longest ASDU fits */
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM + ZZB_APS_HDR_MAX_LEN);
	net_buf_add_u8(buf, tsn);
	net_buf_add_mem(buf, payload, len);
	zzb_frame_meta(buf)->handle = handle;

	err = zzb_apsde_data_req(node, buf, &req);
	if (err) {
		net_buf_unref(buf);
	}

	return err;
}

void zzb_zdo_respond(struct zzb_node *node, const struct zzb_zdo_req *req, const uint8_t *payload,
		     size_t len)
{
	zzb_zdo_send(node, req->src_addr, req->cluster | ZZB_ZDP_RSP, req->tsn, payload, len,
		     ZZB_ZDO_HANDLE);
}

void zzb_apsde_data_conf(struct zzb_node *node, uint8_t handle, uint8_t status)
{
}

void zzb_apsde_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind)
{
	struct zzb_zdo_req req;

	/* Application endpoints wait for ZCL */
	if (ind->dst_ep != 0 || ind->profile != 0x0000 || buf->len == 0 ||
	    (ind->cluster & ZZB_ZDP_RSP)) {
		net_buf_unref(buf);
		return;
	}

	req = (struct zzb_zdo_req){
		.cluster = ind->cluster,
		.src_addr = ind->src_addr,
		.broadcast = zzb_nwk_is_broadcast(ind->dst_addr),
		.tsn = buf->data[0],
		.payload = buf->data + 1,
		.len = buf->len - 1,
	};
	zzb_zdo_server(node, &req);
	net_buf_unref(buf);
}
