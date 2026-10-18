/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "common/buf.h"
#include "common/node.h"
#include "mac/frame.h"
#include "mac/mac.h"
#include "radio/port.h"

/* macMaxFrameRetries */
#define MAX_FRAME_RETRIES 3

/* aMaxPHYPacketSize without the FCS */
#define MAX_FRAME_LEN 125

struct zzb_mac_addr zzb_mac_src_addr(const struct zzb_node *node)
{
	if (node->mac.short_addr < 0xfffe) {
		return (struct zzb_mac_addr){
			.mode = ZZB_MAC_ADDR_SHORT,
			.pan_id = node->mac.pan_id,
			.short_addr = node->mac.short_addr,
		};
	}

	return (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_EXT,
		.pan_id = node->mac.pan_id,
		.ext_addr = node->eui64,
	};
}

static void enqueue(struct zzb_node *node, struct net_buf *buf)
{
	bool idle = k_fifo_is_empty(&node->mac.tx_fifo);

	k_fifo_put(&node->mac.tx_fifo, buf);
	if (idle) {
		zzb_radio_tx(&node->port, buf);
	}
}

int zzb_mcps_data_req(struct zzb_node *node, struct net_buf *buf)
{
	struct zzb_frame_meta *meta = zzb_frame_meta(buf);
	struct zzb_mac_hdr hdr = {
		.type = ZZB_MAC_FRAME_DATA,
		.flags = meta->flags & ZZB_FRAME_ACK_REQ,
		.dst = meta->dst,
		.src = zzb_mac_src_addr(node),
	};
	size_t hdr_len = zzb_mac_hdr_len(&hdr);

	if (net_buf_headroom(buf) < hdr_len || hdr_len + buf->len > MAX_FRAME_LEN) {
		return -EMSGSIZE;
	}

	hdr.seq = node->mac.dsn++;
	zzb_mac_hdr_write(&hdr, net_buf_push(buf, hdr_len));
	enqueue(node, buf);
	return 0;
}

void zzb_mac_send_cmd(struct zzb_node *node, struct zzb_mac_hdr *hdr, const struct zzb_mac_cmd *cmd)
{
	struct net_buf *buf = node->mac.cmd_buf;
	size_t len;

	hdr->type = ZZB_MAC_FRAME_CMD;
	hdr->seq = node->mac.dsn++;
	net_buf_reset(buf);
	len = zzb_mac_hdr_write(hdr, buf->data);
	len += zzb_mac_cmd_write(cmd, buf->data + len);
	net_buf_add(buf, len);
	enqueue(node, buf);
}

void zzb_radio_tx_done(struct zzb_radio_port *port, struct net_buf *buf, enum zzb_mac_status status,
		       bool frame_pending)
{
	struct zzb_node *node = CONTAINER_OF(port, struct zzb_node, port);
	struct zzb_mac *mac = &node->mac;
	uint8_t handle = zzb_frame_meta(buf)->handle;
	struct net_buf *next;

	/* Same frame, same sequence number (802.15.4 5.1.6.4.3) */
	if (status == ZZB_MAC_STATUS_NO_ACK && mac->tx_retries < MAX_FRAME_RETRIES) {
		mac->tx_retries++;
		zzb_radio_tx(port, buf);
		return;
	}

	mac->tx_retries = 0;
	k_fifo_get(&mac->tx_fifo, K_NO_WAIT);
	next = k_fifo_peek_head(&mac->tx_fifo);
	if (next != NULL) {
		zzb_radio_tx(port, next);
	}

	if (buf == mac->cmd_buf) {
		zzb_mac_cmd_done(node, status, frame_pending);
		return;
	}

	net_buf_unref(buf);
	zzb_mcps_data_conf(node, handle, status);
}
