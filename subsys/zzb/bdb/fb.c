/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/bdb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "zcl/zcl.h"
#include "zdo/zdo.h"

/* Identify cluster (ZCL 3.5): Identify Query */
#define CLUSTER_IDENTIFY   0x0003
#define CMD_IDENTIFY_QUERY 0x01

/* Groups cluster (ZCL 3.6): Add Group If Identifying */
#define CLUSTER_GROUPS               0x0004
#define CMD_ADD_GROUP_IF_IDENTIFYING 0x05

/* The wait for Identify Query Response; BDB 8.5 does not define it */
#define QUERY_WAIT_MS 5000

/* The group of unicast bindings */
#define NO_GROUP 0xffff

/* Whether endpoint ep has an instance of cluster */
static bool has_cluster(const struct zzb_node *node, uint8_t ep, uint16_t cluster, bool server)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);

	for (int i = 0; e != NULL && i < e->cluster_count; i++) {
		if (e->clusters[i].id == cluster && e->clusters[i].server == server) {
			return true;
		}
	}

	return false;
}

/* BDB 8.4 */
static int target(struct zzb_node *node, uint8_t ep)
{
	if (!has_cluster(node, ep, CLUSTER_IDENTIFY, true)) {
		return -ENOENT;
	}

	if (node->bdb.fb_target_ep != 0) {
		return -EBUSY;
	}

	/* Step 1 */
	if (!node->bdb.on_network) {
		zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_TARGET,
			       ZZB_BDB_NOT_ON_A_NETWORK);
		return 0;
	}

	/* Step 3; other identifying endpoints are not changed (MAY) */
	node->bdb.fb_target_ep = ep;
	return zzb_zcl_identify(node, ep, ZZB_BDB_MIN_COMMISSIONING_S);
}

/* Step 5 */
void zzb_bdb_identify_end(struct zzb_node *node, uint8_t ep)
{
	if (ep == 0 || ep != node->bdb.fb_target_ep) {
		return;
	}

	node->bdb.fb_target_ep = 0;
	zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_SUCCESS);
}

static void finish(struct zzb_node *node, enum zzb_bdb_status status)
{
	node->bdb.fb.ep = 0;
	k_work_cancel_delayable(&node->bdb.fb.timer);
	zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, status);
}

/* BDB 8.5 step 10, then step 11; Identify with IdentifyTime 0 to the targets is not sent (MAY) */
static void done(struct zzb_node *node)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;
	uint8_t payload[3] = {0};

	/* The broadcast form needs no response handling */
	if (fb->group_bound) {
		sys_put_le16(fb->group, payload);
		zzb_zcl_send_cmd(node, fb->ep, 0xffff, 0xff, CLUSTER_GROUPS,
				 CMD_ADD_GROUP_IF_IDENTIFYING, payload, sizeof(payload));
	}

	finish(node, ZZB_BDB_SUCCESS);
}

/* Step 5 for the respondent next; a request that cannot be sent passes to the next one */
static void request_simple_desc(struct zzb_node *node)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;

	fb->state = ZZB_BDB_FB_SIMPLE_DESC;
	for (; fb->next < fb->count; fb->next++) {
		const struct zzb_bdb_respondent *r = &fb->respondents[fb->next];

		if (zzb_zdo_simple_desc_req(node, r->addr, r->ep) == 0) {
			return;
		}
	}

	done(node);
}

/* Step 11 */
static void next_respondent(struct zzb_node *node)
{
	node->bdb.fb.next++;
	request_simple_desc(node);
}

/* Step 4 */
static void timer_handler(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, bdb.fb.timer);

	if (node->bdb.fb.count == 0) {
		finish(node, ZZB_BDB_NO_IDENTIFY_QUERY_RESPONSE);
		return;
	}

	request_simple_desc(node);
}

void zzb_bdb_identify_query_rsp(struct zzb_node *node, uint8_t ep, uint16_t addr, uint8_t src_ep)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;

	if (ep == 0 || ep != fb->ep || fb->state != ZZB_BDB_FB_QUERY ||
	    fb->count == ARRAY_SIZE(fb->respondents)) {
		return;
	}

	for (int i = 0; i < fb->count; i++) {
		if (fb->respondents[i].addr == addr && fb->respondents[i].ep == src_ep) {
			return;
		}
	}

	fb->respondents[fb->count++] = (struct zzb_bdb_respondent){
		.addr = addr,
		.ep = src_ep,
	};
}

static bool listed(const uint8_t *list, uint8_t count, uint16_t cluster)
{
	for (int i = 0; i < count; i++) {
		if (sys_get_le16(list + 2 * i) == cluster) {
			return true;
		}
	}

	return false;
}

/*
 * Step 7: a client cluster of the initiator matches an input cluster of the respondent, a server
 * cluster an output cluster. Identify serves the procedure itself and is not bound.
 */
static void match(struct zzb_node *node, const struct zzb_zdo_simple_desc *desc)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, fb->ep);

	fb->cluster_count = 0;
	if (desc->profile != ep->profile) {
		return;
	}

	for (int i = 0; i < ep->cluster_count && fb->cluster_count < ARRAY_SIZE(fb->clusters);
	     i++) {
		const struct zzb_zcl_cluster *cl = &ep->clusters[i];
		bool found = cl->server ? listed(desc->out, desc->out_count, cl->id)
					: listed(desc->in, desc->in_count, cl->id);

		if (cl->id != CLUSTER_IDENTIFY && found) {
			fb->clusters[fb->cluster_count++] = cl->id;
		}
	}
}

/* Step 8 for the matched clusters; false when the binding table is full */
static bool bind(struct zzb_node *node, const struct zzb_bdb_respondent *r)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;
	struct zzb_aps_binding binding = {
		.src_ep = fb->ep,
		.dst_mode = fb->group == NO_GROUP ? ZZB_APS_ADDR_EXT : ZZB_APS_ADDR_GROUP,
		.dst_addr = fb->group,
		.dst_ext = r->ext_addr,
		.dst_ep = r->ep,
	};

	for (int i = 0; i < fb->cluster_count; i++) {
		binding.cluster = fb->clusters[i];
		if (zzb_apsme_bind_req(node, &binding) == -ENOMEM) {
			finish(node, ZZB_BDB_BINDING_TABLE_FULL);
			return false;
		}
	}

	fb->group_bound |= fb->group != NO_GROUP && fb->cluster_count > 0;
	return true;
}

/* The IEEE address of addr from a respondent before, or 0 */
static uint64_t known_ext_addr(const struct zzb_bdb_fb *fb, uint16_t addr)
{
	for (int i = 0; i < fb->next; i++) {
		if (fb->respondents[i].addr == addr && fb->respondents[i].ext_addr != 0) {
			return fb->respondents[i].ext_addr;
		}
	}

	return 0;
}

/* Steps 6 to 9 */
void zzb_zdo_simple_desc_conf(struct zzb_node *node, uint8_t status, uint16_t addr,
			      const struct zzb_zdo_simple_desc *desc)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;
	struct zzb_bdb_respondent *r = &fb->respondents[fb->next];

	if (fb->ep == 0 || fb->state != ZZB_BDB_FB_SIMPLE_DESC || addr != r->addr) {
		return;
	}

	if (status != ZZB_ZDP_SUCCESS) {
		next_respondent(node);
		return;
	}

	match(node, desc);
	r->ext_addr = known_ext_addr(fb, addr);
	if (fb->cluster_count == 0 || fb->group != NO_GROUP || r->ext_addr != 0) {
		if (bind(node, r)) {
			next_respondent(node);
		}

		return;
	}

	/* A unicast binding needs the IEEE address of the respondent */
	fb->state = ZZB_BDB_FB_IEEE_ADDR;
	if (zzb_zdo_ieee_addr_req(node, addr) != 0) {
		next_respondent(node);
	}
}

void zzb_zdo_ieee_addr_conf(struct zzb_node *node, uint8_t status, uint16_t addr, uint64_t ext_addr)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;
	struct zzb_bdb_respondent *r = &fb->respondents[fb->next];

	if (fb->ep == 0 || fb->state != ZZB_BDB_FB_IEEE_ADDR || addr != r->addr) {
		return;
	}

	if (status != ZZB_ZDP_SUCCESS) {
		next_respondent(node);
		return;
	}

	r->ext_addr = ext_addr;
	if (bind(node, r)) {
		next_respondent(node);
	}
}

/* BDB 8.5 steps 1 to 3 */
static int initiator(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	struct zzb_bdb_fb *fb = &node->bdb.fb;

	if (group != NO_GROUP && (group == 0 || group > 0xfff7)) {
		return -EINVAL;
	}

	if (!has_cluster(node, ep, CLUSTER_IDENTIFY, false)) {
		return -ENOENT;
	}

	if (fb->ep != 0) {
		return -EBUSY;
	}

	if (!node->bdb.on_network) {
		zzb_bdb_notify(node, ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR,
			       ZZB_BDB_NOT_ON_A_NETWORK);
		return 0;
	}

	fb->ep = ep;
	fb->group = group;
	fb->state = ZZB_BDB_FB_QUERY;
	fb->count = 0;
	fb->next = 0;
	fb->group_bound = false;

	/* A query that cannot be sent gets no response */
	zzb_zcl_send_cmd(node, ep, 0xffff, 0xff, CLUSTER_IDENTIFY, CMD_IDENTIFY_QUERY, NULL, 0);
	k_work_reschedule_for_queue(&zzb_workq, &fb->timer, K_MSEC(QUERY_WAIT_MS));
	return 0;
}

void zzb_bdb_fb_clear(struct zzb_node *node)
{
	node->bdb.fb_target_ep = 0;
	node->bdb.fb.ep = 0;
	k_work_cancel_delayable(&node->bdb.fb.timer);
}

void zzb_bdb_fb_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->bdb.fb.timer, timer_handler);
}

struct fb_call {
	struct k_work work;
	struct zzb_node *node;
	uint8_t ep;
	uint16_t group;
	int err;
};

static void target_handler(struct k_work *work)
{
	struct fb_call *call = CONTAINER_OF(work, struct fb_call, work);

	call->err = target(call->node, call->ep);
}

int zzb_bdb_finding_binding_target(struct zzb_node *node, uint8_t ep)
{
	struct fb_call call = {
		.node = node,
		.ep = ep,
	};

	zzb_call_sync(&call.work, target_handler);
	return call.err;
}

static void initiator_handler(struct k_work *work)
{
	struct fb_call *call = CONTAINER_OF(work, struct fb_call, work);

	call->err = initiator(call->node, call->ep, call->group);
}

int zzb_bdb_finding_binding_initiator(struct zzb_node *node, uint8_t ep, uint16_t group)
{
	struct fb_call call = {
		.node = node,
		.ep = ep,
		.group = group,
	};

	zzb_call_sync(&call.work, initiator_handler);
	return call.err;
}
