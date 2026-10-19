/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

#include "aps/aps.h"
#include "common/buf.h"
#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "zcl/frame.h"
#include "zcl/zcl.h"

static bool accepts(const struct zzb_endpoint *ep, uint16_t profile)
{
	return profile == ep->profile || profile == 0xffff;
}

const struct zzb_zcl_cluster *zzb_zcl_find_cluster(const struct zzb_endpoint *ep, uint16_t id,
						   bool server)
{
	for (int i = 0; i < ep->cluster_count; i++) {
		if (ep->clusters[i].id == id && ep->clusters[i].server == server) {
			return &ep->clusters[i];
		}
	}

	return NULL;
}

int zzb_zcl_send(struct zzb_node *node, const struct zzb_apsde_req *aps,
		 const struct zzb_zcl_hdr *hdr, const uint8_t *payload, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	int err;

	if (buf == NULL) {
		return -ENOBUFS;
	}

	/* ZCL frames have no APS security, so the longest ASDU fits */
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM + ZZB_APS_HDR_MAX_LEN);
	zzb_zcl_hdr_write(hdr, net_buf_add(buf, zzb_zcl_hdr_len(hdr)));
	net_buf_add_mem(buf, payload, len);
	zzb_frame_meta(buf)->handle = ZZB_ZCL_HANDLE;

	err = zzb_apsde_data_req(node, buf, aps);
	if (err) {
		net_buf_unref(buf);
	}

	return err;
}

int zzb_zcl_send_cmd(struct zzb_node *node, uint8_t src_ep, uint16_t dst_addr, uint8_t dst_ep,
		     uint16_t cluster, uint8_t cmd, const uint8_t *payload, size_t len)
{
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, src_ep);
	struct zzb_zcl_hdr hdr = {
		.type = ZZB_ZCL_FRAME_CLUSTER,
		.flags = ZZB_ZCL_DISABLE_DEFAULT_RSP,
		.cmd = cmd,
	};
	struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = dst_addr,
		.dst_ep = dst_ep,
		.cluster = cluster,
		.src_ep = src_ep,
		.tx_options = zzb_nwk_is_broadcast(dst_addr) ? 0 : ZZB_APS_TX_ACK,
	};

	if (ep == NULL) {
		return -ENOENT;
	}

	hdr.tsn = node->zcl.tsn++;
	aps.profile = ep->profile;
	return zzb_zcl_send(node, &aps, &hdr, payload, len);
}

int zzb_zcl_send_bindings(struct zzb_node *node, const struct zzb_endpoint *ep, uint16_t cluster,
			  enum zzb_zcl_frame_type type, bool to_client, uint8_t cmd,
			  const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_hdr hdr = {
		.type = type,
		.flags = ZZB_ZCL_DISABLE_DEFAULT_RSP | (to_client ? ZZB_ZCL_TO_CLIENT : 0),
		.tsn = node->zcl.tsn++,
		.cmd = cmd,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_BINDING,
		.profile = ep->profile,
		.cluster = cluster,
		.src_ep = ep->id,
		.tx_options = ZZB_APS_TX_ACK,
	};

	return zzb_zcl_send(node, &aps, &hdr, payload, len);
}

struct send_bound_req {
	struct k_work work;
	struct zzb_node *node;
	uint8_t ep;
	uint16_t cluster;
	uint8_t cmd;
	const uint8_t *payload;
	size_t len;
	int err;
};

static void send_bound_handler(struct k_work *work)
{
	struct send_bound_req *req = CONTAINER_OF(work, struct send_bound_req, work);
	const struct zzb_endpoint *ep = zzb_node_endpoint(req->node, req->ep);

	if (ep == NULL || zzb_zcl_find_cluster(ep, req->cluster, false) == NULL) {
		req->err = -ENOENT;
		return;
	}

	req->err = zzb_zcl_send_bindings(req->node, ep, req->cluster, ZZB_ZCL_FRAME_CLUSTER, false,
					 req->cmd, req->payload, req->len);
}

int zzb_zcl_send_bound(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t cmd,
		       const uint8_t *payload, size_t len)
{
	struct send_bound_req req = {
		.node = node,
		.ep = ep,
		.cluster = cluster,
		.cmd = cmd,
		.payload = payload,
		.len = len,
	};

	zzb_call_sync(&req.work, send_bound_handler);
	return req.err;
}

void zzb_zcl_respond(struct zzb_node *node, const struct zzb_zcl_req *req,
		     enum zzb_zcl_frame_type type, uint8_t cmd, const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_hdr hdr = {
		.type = type,
		.flags = ZZB_ZCL_DISABLE_DEFAULT_RSP | (req->hdr.flags & ZZB_ZCL_MANUFACTURER) |
			 ((req->hdr.flags & ZZB_ZCL_TO_CLIENT) ? 0 : ZZB_ZCL_TO_CLIENT),
		.manufacturer = req->hdr.manufacturer,
		.tsn = req->hdr.tsn,
		.cmd = cmd,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = req->cmd.src_addr,
		.dst_ep = req->cmd.src_ep,
		.profile = req->profile,
		.cluster = req->cmd.cluster,
		.src_ep = req->cmd.ep,
		.tx_options = ZZB_APS_TX_ACK,
	};

	zzb_zcl_send(node, &aps, &hdr, payload, len);
}

void zzb_zcl_cmd_respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t id,
			 const uint8_t *payload, size_t len)
{
	zzb_zcl_respond(node, CONTAINER_OF(cmd, struct zzb_zcl_req, cmd), ZZB_ZCL_FRAME_CLUSTER, id,
			payload, len);
}

int zzb_zcl_client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	return ZZB_ZCL_SUCCESS;
}

void zzb_zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		    uint8_t variant)
{
	if (node->zcl_effect_cb != NULL) {
		node->zcl_effect_cb(node, ep, cluster, effect, variant);
	}
}

/* ZCL 2.5.12.2 */
static void default_rsp(struct zzb_node *node, const struct zzb_zcl_req *req, int status)
{
	const uint8_t rsp[] = {req->hdr.cmd, status};

	if (status == ZZB_ZCL_RESPONDED || !req->cmd.unicast) {
		return;
	}

	if (status == ZZB_ZCL_SUCCESS && (req->hdr.flags & ZZB_ZCL_DISABLE_DEFAULT_RSP)) {
		return;
	}

	zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_DEFAULT_RSP, rsp, sizeof(rsp));
}

static int process(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	const struct zzb_zcl_cluster *cl = req->cluster;

	if (cl == NULL) {
		return ZZB_ZCL_UNSUPPORTED_CLUSTER;
	}

	/* No manufacturer-specific attributes or commands */
	if (req->hdr.flags & ZZB_ZCL_MANUFACTURER) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (req->hdr.type == ZZB_ZCL_FRAME_GLOBAL) {
		return zzb_zcl_foundation(node, req);
	}

	if (cl->def == NULL || cl->def->cmd == NULL) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	return cl->def->cmd(node, &req->cmd, cl->values);
}

static void receive(struct zzb_node *node, const struct zzb_endpoint *ep,
		    const struct zzb_apsde_ind *ind, const struct zzb_zcl_hdr *hdr,
		    const uint8_t *payload, size_t len)
{
	const struct zzb_zcl_req req = {
		.cmd.ep = ep->id,
		.cmd.cluster = ind->cluster,
		.cmd.src_addr = ind->src_addr,
		.cmd.src_ep = ind->src_ep,
		.cmd.tsn = hdr->tsn,
		.cmd.id = hdr->cmd,
		.cmd.payload = payload,
		.cmd.len = len,
		.hdr = *hdr,
		.profile = ep->profile,
		.cmd.unicast =
			!ind->group && !zzb_nwk_is_broadcast(ind->dst_addr) && ind->dst_ep != 0xff,
		.cluster =
			zzb_zcl_find_cluster(ep, ind->cluster, !(hdr->flags & ZZB_ZCL_TO_CLIENT)),
	};
	int status;

	/* A Default Response is never answered (ZCL 2.5.12.2) */
	if (hdr->type == ZZB_ZCL_FRAME_GLOBAL && hdr->cmd == ZZB_ZCL_DEFAULT_RSP) {
		return;
	}

	status = process(node, &req);

	/* ZCL 2.5.6.3 */
	if (hdr->type == ZZB_ZCL_FRAME_GLOBAL && hdr->cmd == ZZB_ZCL_WRITE_NO_RSP) {
		return;
	}

	default_rsp(node, &req, status);
}

/* Whether a frame to the broadcast endpoint or a group is for endpoint ep */
static bool addressed(struct zzb_node *node, const struct zzb_apsde_ind *ind,
		      const struct zzb_endpoint *ep)
{
	return !ind->group || zzb_aps_group_member(node, ind->dst_addr, ep->id);
}

static void dispatch(struct zzb_node *node, const struct zzb_apsde_ind *ind, const uint8_t *frame,
		     size_t len)
{
	const struct zzb_endpoint *ep;
	struct zzb_zcl_hdr hdr;
	int hdr_len = zzb_zcl_hdr_parse(frame, len, &hdr);

	if (hdr_len < 0) {
		return;
	}

	/* ZCL 2.3.2.1, R22 2.2.8.4.2 */
	if (ind->group || ind->dst_ep == 0xff) {
		for (size_t i = 0; i < node->device->endpoint_count; i++) {
			ep = &node->device->endpoints[i];
			if (addressed(node, ind, ep) && accepts(ep, ind->profile) &&
			    zzb_zcl_find_cluster(ep, ind->cluster,
						 !(hdr.flags & ZZB_ZCL_TO_CLIENT))) {
				receive(node, ep, ind, &hdr, frame + hdr_len, len - hdr_len);
			}
		}

		return;
	}

	ep = zzb_node_endpoint(node, ind->dst_ep);
	if (ep != NULL && accepts(ep, ind->profile)) {
		receive(node, ep, ind, &hdr, frame + hdr_len, len - hdr_len);
	}
}

void zzb_zcl_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind)
{
	dispatch(node, ind, buf->data, buf->len);
	net_buf_unref(buf);
}

void zzb_zcl_init(struct zzb_node *node)
{
	/* Safe on the zeroed struct of a new node */
	k_work_cancel_delayable(&node->zcl.identify_timer);
	k_work_cancel_delayable(&node->zcl.on_off_timer);
	k_work_cancel_delayable(&node->zcl.report_timer);
	k_work_cancel_delayable(&node->zcl.level_timer);
	k_work_cancel_delayable(&node->zcl.mains_alarm_timer);
	k_work_cancel_delayable(&node->zcl.temp_alarm_timer);
	node->zcl = (struct zzb_zcl){
		.tsn = sys_rand8_get(),
	};
	zzb_zcl_identify_init(node);
	zzb_zcl_on_off_init(node);
	zzb_zcl_report_init(node);
	zzb_zcl_level_init(node);
	zzb_zcl_power_config_init(node);
	zzb_zcl_device_temp_init(node);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		for (int j = 0; j < ep->cluster_count; j++) {
			const struct zzb_zcl_cluster *cl = &ep->clusters[j];

			if (cl->def != NULL && cl->def->init != NULL) {
				cl->def->init(cl->values);
			}
		}
	}
}

void zzb_zcl_start(struct zzb_node *node)
{
	/* Before the start hooks, so that a change of StartUpOnOff is reported */
	zzb_zcl_report_start(node);

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		for (int j = 0; j < ep->cluster_count; j++) {
			const struct zzb_zcl_cluster *cl = &ep->clusters[j];

			if (cl->def != NULL && cl->def->start != NULL) {
				cl->def->start(node, ep->id, cl->values);
			}
		}
	}
}

void zzb_zcl_reset(struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		for (int j = 0; j < ep->cluster_count; j++) {
			const struct zzb_zcl_cluster *cl = &ep->clusters[j];

			if (cl->def != NULL && cl->def->reset != NULL) {
				cl->def->reset(node, ep->id, cl->values);
			}
		}
	}

	zzb_zcl_alarms_reset(node);
	/* After the reset hooks, which may have armed the reporting timer */
	zzb_zcl_report_reset(node);
	zzb_zcl_scenes_delete(node);
	zzb_zcl_attr_delete(node);
}
