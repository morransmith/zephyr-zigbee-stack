/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/ota.h>

#include "aps/aps.h"
#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 11.3.1.1) */
#define REVISION 4

/* CurrentZigBeeStackVersion of ZigBee PRO (ZCL Table 11-6) */
#define STACK_PRO 0x0002

/* The server is the trust center, on endpoint 1 before the first Image Notify */
#define SERVER    0x0000
#define SERVER_EP 1

#define INTERVAL K_MINUTES(CONFIG_ZZB_ZCL_OTA_QUERY_INTERVAL)

/* The next query after one that APSDE-DATA refuses */
#define RETRY K_SECONDS(60)

/* The length of the OTA header (ZCL 11.4.2) */
#define HEADER_LEN 56

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_UPGRADE_SERVER_ID, ZZB_ZCL_EUI64, 0, struct zzb_zcl_ota,
		     upgrade_server_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_FILE_OFFSET, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota, file_offset),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_CURRENT_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     current_file_version),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_OTA_CURRENT_STACK_VERSION, ZZB_ZCL_UINT16, STACK_PRO),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     downloaded_file_version),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_ota,
		     image_upgrade_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_MANUFACTURER_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     manufacturer_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_TYPE_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     image_type_id),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The defaults outside a download (ZCL Table 11-10); the application sets the running image */
static void init(void *values)
{
	struct zzb_zcl_ota *v = values;

	v->file_offset = 0xffffffff;
	v->downloaded_file_version = 0xffffffff;
	v->image_upgrade_status = ZZB_ZCL_OTA_NORMAL;
	v->image_type_id = 0xffff;
	v->server_ep = SERVER_EP;
}

static void refresh(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_ota *v = values;

	v->upgrade_server_id = zzb_apsme_get_tc_addr(node);
	v->manufacturer_id = node->device->manufacturer_code;
}

/* The first endpoint with an OTA client, which downloads */
static const struct zzb_endpoint *first(const struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		if (zzb_zcl_find_cluster(ep, ZZB_ZCL_OTA, false) != NULL) {
			return ep;
		}
	}

	return NULL;
}

static struct zzb_zcl_ota *values_of(const struct zzb_endpoint *ep)
{
	return zzb_zcl_find_cluster(ep, ZZB_ZCL_OTA, false)->values;
}

static void arm(struct zzb_node *node, k_timeout_t delay)
{
	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.ota_timer, delay);
}

/* Sends the command id with the payload from the client on ep to the server */
static int request(struct zzb_node *node, const struct zzb_endpoint *ep, uint8_t id,
		   const uint8_t *payload, size_t len)
{
	struct zzb_zcl_ota *v = values_of(ep);
	const struct zzb_zcl_hdr hdr = {
		.type = ZZB_ZCL_FRAME_CLUSTER,
		.tsn = node->zcl.tsn++,
		.cmd = id,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = SERVER,
		.dst_ep = v->server_ep,
		.profile = ep->profile,
		.cluster = ZZB_ZCL_OTA,
		.src_ep = ep->id,
		.tx_options = ZZB_APS_TX_ACK,
	};

	v->tsn = hdr.tsn;
	return zzb_zcl_send(node, &aps, &hdr, payload, len);
}

/* Query Next Image Request (ZCL 11.13.4); a sleepy node polls fast for the response */
static void query(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);
	uint8_t payload[9] = {0};

	sys_put_le16(node->device->manufacturer_code, payload + 1);
	sys_put_le16(v->image_type, payload + 3);
	sys_put_le32(v->current_file_version, payload + 5);
	if (request(node, ep, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE, payload, sizeof(payload)) != 0) {
		arm(node, RETRY);
		return;
	}

	zzb_nwk_poll_fast(node);
	arm(node, INTERVAL);
}

/* Armed only on a node with an OTA client */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.ota_timer);

	query(node, first(node));
}

/* ZCL 11.13.5 */
static int query_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ota *v)
{
	const uint8_t *p = cmd->payload;

	if (cmd->tsn != v->tsn) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	/* NO_IMAGE_AVAILABLE and NOT_AUTHORIZED end the query */
	if (p[0] != ZZB_ZCL_SUCCESS) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 13 || sys_get_le16(p + 1) != node->device->manufacturer_code ||
	    sys_get_le16(p + 3) != v->image_type || sys_get_le32(p + 9) < HEADER_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	return ZZB_ZCL_SUCCESS;
}

/* Only the client on the first endpoint processes commands, and only those of the trust center */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (first(node)->id != cmd->ep) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->src_addr != SERVER) {
		return ZZB_ZCL_SUCCESS;
	}

	switch (cmd->id) {
	case ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP:
		return query_rsp(node, cmd, values);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* Nothing is sent at a join; the first query follows one interval after the start */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	if (first(node)->id == ep) {
		arm(node, INTERVAL);
	}
}

/* The reset of the node restarts the query interval */
static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	if (first(node)->id == ep) {
		arm(node, INTERVAL);
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_ota_client_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.cmd = cmd,
	.refresh = refresh,
};

void zzb_zcl_ota_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.ota_timer, tick);
}
