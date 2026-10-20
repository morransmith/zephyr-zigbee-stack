/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "aps/aps.h"
#include "aps/frame.h"
#include "common/buf.h"
#include "common/queue.h"
#include "fake_radio.h"
#include "mac/frame.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"
#include "parent.h"
#include "sec/aux_hdr.h"
#include "sec/ccm.h"
#include "sec/hmac.h"
#include "sec/key_cache.h"

/* A coordinator that permits association */
#define SUPERFRAME (0x0fff | ZZB_MAC_SF_PAN_COORD | ZZB_MAC_SF_ASSOC_PERMIT)

const uint8_t nwk_key[ZZB_SEC_KEY_LEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

const uint8_t nwk_key_1[ZZB_SEC_KEY_LEN] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

const uint8_t tc_link_key_b[ZZB_SEC_KEY_LEN] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

/* On/Off command Toggle */
static const uint8_t asdu[] = {0x01, 0x42, 0x02};

struct parent_log parent_log;

static struct {
	uint16_t short_addr;
	uint64_t eui64;
	uint16_t child_addr;
	uint64_t child_eui64;
	uint8_t parent_info;
	bool assoc_pending;
	uint8_t dsn;
	uint8_t nwk_seq;
	uint32_t fc;
	uint8_t aps_counter;
	uint32_t aps_fc;
	/* The trust center link key of the child */
	const uint8_t *link_key;
	int withheld_acks;
	/* Stack compliance revision of Node_Desc_rsp */
	uint8_t stack_revision;
	/* Sends the network key after an association or a trust center rejoin */
	bool key_after_join;
	/* The association or rejoin request of the child has RxOnWhenIdle 0 */
	bool child_sleepy;
} parent;

/* macTransactionPersistenceTime of 500 superframe periods (R22 nwkTransactionPersistenceTime) */
#define PERSISTENCE_MS 7680

/* Data frames for a sleepy child, sent at its data requests (802.15.4 5.1.6.3) */
static struct {
	struct zzb_mac_hdr hdr;
	uint8_t payload[ZZB_BUF_SIZE];
	size_t len;
	int64_t time;
} queue[8];
static size_t queue_len;

/* Sends the network key KEY_DELAY_MS after a join, when the child has its address */
static struct k_work_delayable key_work;

/* The delay of the Transport Key after the association on a CC2531 (Z-Stack Home 1.2) */
#define KEY_DELAY_MS 130

static void schedule_key(void)
{
	if (parent.key_after_join) {
		k_work_reschedule_for_queue(&zzb_workq, &key_work, K_MSEC(KEY_DELAY_MS));
	}
}

/* ZDP clusters (R22 Tables 2-44, 2-80) */
#define IEEE_ADDR_REQ           0x0001
#define NODE_DESC_REQ           0x0002
#define SIMPLE_DESC_REQ         0x0004
#define DEVICE_ANNCE            0x0013
#define MGMT_PERMIT_JOINING_REQ 0x0036
#define MGMT_LEAVE_REQ          0x0034
#define MGMT_LEAVE_RSP          0x8034

static struct zzb_mac_addr short_addr(uint16_t addr)
{
	return (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = PAN_ID,
		.short_addr = addr,
	};
}

static struct zzb_mac_addr ext_addr(uint64_t addr)
{
	return (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_EXT,
		.pan_id = PAN_ID,
		.ext_addr = addr,
	};
}

static void transmit(struct zzb_mac_hdr *hdr, const uint8_t *payload, size_t len)
{
	uint8_t frame[ZZB_BUF_SIZE];
	size_t hdr_len;

	hdr->seq = parent.dsn++;
	hdr_len = zzb_mac_hdr_write(hdr, frame);
	memcpy(frame + hdr_len, payload, len);
	fake_radio_send(0, frame, hdr_len + len);
}

static bool for_child(const struct zzb_mac_addr *dst)
{
	return (dst->mode == ZZB_MAC_ADDR_SHORT && dst->short_addr == parent.child_addr) ||
	       (dst->mode == ZZB_MAC_ADDR_EXT && dst->ext_addr == parent.child_eui64);
}

static void send_mac(struct zzb_mac_hdr *hdr, const uint8_t *payload, size_t len)
{
	if (!parent.child_sleepy || hdr->type != ZZB_MAC_FRAME_DATA || !for_child(&hdr->dst)) {
		transmit(hdr, payload, len);
		return;
	}

	zassert_true(queue_len < ARRAY_SIZE(queue));
	queue[queue_len].hdr = *hdr;
	memcpy(queue[queue_len].payload, payload, len);
	queue[queue_len].len = len;
	queue[queue_len].time = k_uptime_get();
	queue_len++;
}

static void dequeue(void)
{
	queue_len--;
	memmove(&queue[0], &queue[1], queue_len * sizeof(queue[0]));
}

/* Sends the oldest frame for the child, with frame pending when more wait; false without one */
static bool send_queued(void)
{
	struct zzb_mac_hdr hdr;

	while (queue_len > 0 && k_uptime_get() - queue[0].time > PERSISTENCE_MS) {
		parent_log.expired++;
		dequeue();
	}

	if (queue_len == 0) {
		return false;
	}

	hdr = queue[0].hdr;
	if (queue_len > 1) {
		hdr.flags |= ZZB_FRAME_PENDING;
	}

	transmit(&hdr, queue[0].payload, queue[0].len);
	dequeue();
	return true;
}

static void send_beacon(void)
{
	struct zzb_mac_hdr hdr = {
		.type = ZZB_MAC_FRAME_BEACON,
		.src = short_addr(parent.short_addr),
	};
	const struct zzb_nwk_beacon beacon = {
		.stack_profile = 2,
		.protocol_version = 2,
		.router_capacity = true,
		.ed_capacity = true,
		.ext_pan_id = EXT_PAN_ID,
		.tx_offset = 0xffffff,
	};
	uint8_t payload[ZZB_BUF_SIZE];
	size_t len = zzb_mac_beacon_write(SUPERFRAME, payload);

	len += zzb_nwk_beacon_write(&beacon, payload + len);
	send_mac(&hdr, payload, len);
}

/* R22 Table D-3, first form */
static void send_assoc_rsp(void)
{
	struct zzb_mac_hdr hdr = {
		.type = ZZB_MAC_FRAME_CMD,
		.flags = ZZB_FRAME_ACK_REQ,
		.dst = ext_addr(parent.child_eui64),
		.src = ext_addr(parent.eui64),
	};
	const struct zzb_mac_cmd cmd = {
		.id = ZZB_MAC_CMD_ASSOC_RSP,
		.assoc_rsp = {.short_addr = parent.child_addr},
	};
	uint8_t payload[8];

	send_mac(&hdr, payload, zzb_mac_cmd_write(&cmd, payload));
}

/* Sends payload to dst through the child with radius 1, secured when flags has ZZB_NWK_SECURITY */
static void send_nwk(enum zzb_nwk_frame_type type, uint16_t flags, uint16_t dst,
		     const uint8_t *payload, size_t payload_len)
{
	struct zzb_mac_hdr mac = {
		.type = ZZB_MAC_FRAME_DATA,
		.flags = ZZB_FRAME_ACK_REQ,
		.dst = short_addr(parent.child_addr),
		.src = short_addr(parent.short_addr),
	};
	const struct zzb_nwk_hdr hdr = {
		.type = type,
		.flags = flags,
		.dst = dst,
		.src = parent.short_addr,
		.radius = 1,
		.seq = parent.nwk_seq++,
		.dst_ext = parent.child_eui64,
		.src_ext = parent.eui64,
	};
	const struct zzb_sec_aux_hdr aux = {
		.key_id = ZZB_SEC_KEY_NWK,
		.ext_nonce = true,
		.frame_counter = parent.fc++,
		.src_addr = parent.eui64,
	};
	bool secure = flags & ZZB_NWK_SECURITY;
	uint8_t frame[ZZB_BUF_SIZE];
	size_t hdr_len = zzb_nwk_hdr_write(&hdr, frame);
	size_t len = hdr_len;
	psa_key_id_t id;

	if (secure) {
		len += zzb_sec_aux_hdr_write(&aux, frame + len);
	}

	memcpy(frame + len, payload, payload_len);
	len += payload_len;
	if (secure) {
		zassert_ok(zzb_sec_key_get(nwk_key, &id));
		zassert_ok(zzb_sec_frame_encrypt(id, parent.eui64, frame, hdr_len, len));
		len += ZZB_SEC_MIC_LEN;
	}

	send_mac(&mac, frame, len);
}

/* Sends cmd to the child with both IEEE addresses */
static void send_nwk_cmd(const struct zzb_nwk_cmd *cmd, bool secure)
{
	uint8_t payload[ZZB_BUF_SIZE];

	send_nwk(ZZB_NWK_FRAME_CMD,
		 ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE | (secure ? ZZB_NWK_SECURITY : 0),
		 parent.child_addr, payload, zzb_nwk_cmd_write(cmd, payload));
}

/* The key of key_id derived from link_key (R22 4.5.3) */
static void frame_key(const uint8_t *link_key, enum zzb_sec_key_id key_id,
		      uint8_t key[ZZB_SEC_KEY_LEN])
{
	if (key_id == ZZB_SEC_KEY_DATA) {
		memcpy(key, link_key, ZZB_SEC_KEY_LEN);
		return;
	}

	zassert_ok(zzb_sec_hmac(link_key, key_id == ZZB_SEC_KEY_TRANSPORT ? 0x00 : 0x02, key));
}

/*
 * Writes an APS frame of the trust center with hdr and payload to p, secured with the key key_id of
 * link_key unless link_key is NULL, and returns its length
 */
static size_t write_aps(uint8_t *p, struct zzb_aps_hdr hdr, const uint8_t *payload,
			size_t payload_len, const uint8_t *link_key, enum zzb_sec_key_id key_id)
{
	const struct zzb_sec_aux_hdr aux = {
		.key_id = key_id,
		.ext_nonce = true,
		.frame_counter = parent.aps_fc++,
		.src_addr = parent.eui64,
	};
	uint8_t key[ZZB_SEC_KEY_LEN];
	size_t hdr_len;
	size_t len;
	psa_key_id_t id;

	hdr.flags |= link_key != NULL ? ZZB_APS_SECURITY : 0;
	hdr_len = zzb_aps_hdr_write(&hdr, p);
	len = hdr_len;
	if (link_key != NULL) {
		len += zzb_sec_aux_hdr_write(&aux, p + len);
	}

	if (payload_len > 0) {
		memcpy(p + len, payload, payload_len);
		len += payload_len;
	}

	if (link_key == NULL) {
		return len;
	}

	frame_key(link_key, key_id, key);
	zassert_ok(zzb_sec_key_get(key, &id));
	zassert_ok(zzb_sec_frame_encrypt(id, parent.eui64, p, hdr_len, len));
	return len + ZZB_SEC_MIC_LEN;
}

/* Sends an APS command to dst with NWK security when nwk_security is set */
static void send_aps_cmd(const struct zzb_aps_cmd *cmd, const uint8_t *link_key,
			 enum zzb_sec_key_id key_id, uint16_t dst, bool nwk_security)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_CMD,
		.counter = parent.aps_counter++,
	};
	uint8_t cmd_bytes[ZZB_BUF_SIZE];
	uint8_t payload[ZZB_BUF_SIZE];

	send_nwk(ZZB_NWK_FRAME_DATA, nwk_security ? ZZB_NWK_SECURITY : 0, dst, payload,
		 write_aps(payload, hdr, cmd_bytes, zzb_aps_cmd_write(cmd, cmd_bytes), link_key,
			   key_id));
}

/* BDB 10.2.5: key B after Request Key, then Confirm Key after Verify Key */
static void reply_aps_cmd(const struct zzb_aps_cmd *cmd)
{
	struct zzb_aps_cmd rsp = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_TC_LINK,
		.transport_key.dst = parent.child_eui64,
		.transport_key.src = parent.eui64,
	};
	uint8_t hash[ZZB_SEC_KEY_LEN];

	switch (cmd->id) {
	case ZZB_APS_CMD_REQUEST_KEY:
		memcpy(rsp.transport_key.key, tc_link_key_b, ZZB_SEC_KEY_LEN);
		send_aps_cmd(&rsp, parent.link_key, ZZB_SEC_KEY_LOAD, parent.child_addr, true);
		parent.link_key = tc_link_key_b;
		break;
	case ZZB_APS_CMD_VERIFY_KEY:
		zassert_ok(zzb_sec_hmac(parent.link_key, 0x03, hash));
		zassert_mem_equal(cmd->verify_key.hash, hash, ZZB_SEC_KEY_LEN);
		rsp = (struct zzb_aps_cmd){
			.id = ZZB_APS_CMD_CONFIRM_KEY,
			.confirm_key.type = ZZB_APS_KEY_TC_LINK,
			.confirm_key.dst = cmd->verify_key.src,
		};
		send_aps_cmd(&rsp, parent.link_key, ZZB_SEC_KEY_DATA, parent.child_addr, true);
		break;
	default:
		break;
	}
}

/* Sends a ZDO frame of cluster to the child, with NWK security only */
static void send_zdo(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_DATA,
		.cluster = cluster,
		.counter = parent.aps_counter++,
	};
	uint8_t payload[ZZB_BUF_SIZE];

	send_nwk(ZZB_NWK_FRAME_DATA, ZZB_NWK_SECURITY, parent.child_addr, payload,
		 write_aps(payload, hdr, asdu, len, NULL, ZZB_SEC_KEY_DATA));
}

/*
 * R22 2.4.4.2.3: the descriptor of a coordinator that is the primary trust center and the network
 * manager, with the stack compliance revision of the test
 */
static void send_node_desc_rsp(uint8_t tsn)
{
	uint8_t rsp[] = {tsn,  0x00, 0x00, 0x00, 0x00, 0x40, 0x8f, 0x00, 0x00,
			 0x50, 0xa0, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00};

	/* The server mask at offset 8 of the descriptor */
	sys_put_le16(BIT(0) | BIT(6) | (parent.stack_revision << 9), rsp + 12);

	send_zdo(NODE_DESC_REQ | 0x8000, rsp, sizeof(rsp));
}

/* R22 2.4.4.2.1, the single device response */
static void send_ieee_addr_rsp(uint8_t tsn)
{
	uint8_t rsp[12] = {tsn};

	sys_put_le64(parent.eui64, rsp + 2);
	sys_put_le16(parent.short_addr, rsp + 10);
	send_zdo(IEEE_ADDR_REQ | 0x8000, rsp, sizeof(rsp));
}

/* R22 2.4.4.2.5: endpoint 1, a Configuration Tool of Home Automation, for any endpoint */
static void send_simple_desc_rsp(uint8_t tsn)
{
	uint8_t rsp[] = {tsn,  0x00, 0x00, 0x00, 0x0e, 0x01, 0x04, 0x01, 0x05, 0x00,
			 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00};

	sys_put_le16(parent.short_addr, rsp + 2);
	send_zdo(SIMPLE_DESC_REQ | 0x8000, rsp, sizeof(rsp));
}

/* Sends a ZCL frame of cluster from endpoint 1 to dst_ep of the child with an ACK request */
static void send_zcl(uint8_t dst_ep, uint16_t cluster, const uint8_t *asdu, size_t len,
		     uint8_t counter)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_DATA,
		.flags = ZZB_APS_ACK_REQ,
		.dst_ep = dst_ep,
		.cluster = cluster,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = counter,
	};
	uint8_t payload[ZZB_BUF_SIZE];

	send_nwk(ZZB_NWK_FRAME_DATA, ZZB_NWK_SECURITY, parent.child_addr, payload,
		 write_aps(payload, hdr, asdu, len, NULL, ZZB_SEC_KEY_DATA));
}

/* Answers Identify Query (ZCL 3.5.2.3.2) with an IdentifyTime of 60 s */
static void reply_zcl(const struct zzb_aps_hdr *hdr, const uint8_t *zcl, size_t len)
{
	uint8_t rsp[] = {0x19, 0x00, 0x00, 0x3c, 0x00};

	/* Cluster-specific, client to server, without manufacturer code */
	if (hdr->cluster != 0x0003 || len < 3 || (zcl[0] & 0x0f) != 0x01 || zcl[2] != 0x01) {
		return;
	}

	parent_log.identify_queries++;
	rsp[1] = zcl[1];
	send_zcl(hdr->src_ep, hdr->cluster, rsp, sizeof(rsp), parent.aps_counter++);
}

/* Logs a ZDO frame of the child to the NWK destination dst and answers Node_Desc_req */
static void rx_zdo(uint16_t cluster, const uint8_t *asdu, size_t len, uint16_t dst)
{
	zassert_true(len > 0);
	parent_log.zdo_cluster = cluster;
	parent_log.zdo_dst = dst;
	switch (cluster) {
	case DEVICE_ANNCE:
		parent_log.device_annces++;
		break;
	case NODE_DESC_REQ:
		parent_log.node_desc_reqs++;
		send_node_desc_rsp(asdu[0]);
		break;
	case IEEE_ADDR_REQ:
		parent_log.ieee_addr_reqs++;
		send_ieee_addr_rsp(asdu[0]);
		break;
	case SIMPLE_DESC_REQ:
		parent_log.simple_desc_reqs++;
		send_simple_desc_rsp(asdu[0]);
		break;
	case MGMT_PERMIT_JOINING_REQ:
		zassert_equal(len, 3);
		parent_log.permit_joining_reqs++;
		parent_log.permit_duration = asdu[1];
		parent_log.tc_significance = asdu[2];
		break;
	case MGMT_LEAVE_RSP:
		parent_log.mgmt_leave_rsps++;
		break;
	default:
		break;
	}
}

/* R22 2.2.5.2.3, with the security of the acknowledged frame */
static void send_aps_ack(const struct zzb_aps_hdr *data)
{
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_ACK,
		.dst_ep = data->src_ep,
		.cluster = data->cluster,
		.profile = data->profile,
		.src_ep = data->dst_ep,
		.counter = data->counter,
	};
	uint8_t payload[ZZB_BUF_SIZE];
	bool secure = data->flags & ZZB_APS_SECURITY;

	send_nwk(ZZB_NWK_FRAME_DATA, ZZB_NWK_SECURITY, parent.child_addr, payload,
		 write_aps(payload, hdr, NULL, 0, secure ? parent.link_key : NULL,
			   ZZB_SEC_KEY_DATA));
}

/* Unsecures an APS frame of the child to dst in place, and logs or answers it */
static void rx_aps(uint8_t *frame, size_t len, uint16_t dst)
{
	struct zzb_sec_aux_hdr aux;
	struct zzb_aps_hdr hdr;
	struct zzb_aps_cmd cmd;
	uint8_t key[ZZB_SEC_KEY_LEN];
	int hdr_len = zzb_aps_hdr_parse(frame, len, &hdr);
	size_t off = hdr_len;
	psa_key_id_t id;
	uint64_t src;

	zassert_true(hdr_len > 0);
	if (hdr.flags & ZZB_APS_SECURITY) {
		off += zzb_sec_aux_hdr_parse(frame + hdr_len, len - hdr_len, &aux);
		src = aux.ext_nonce ? aux.src_addr : parent.child_eui64;
		frame_key(parent.link_key, aux.key_id, key);
		zassert_ok(zzb_sec_key_get(key, &id));
		zassert_ok(zzb_sec_frame_decrypt(id, src, frame, hdr_len, len));
		len -= ZZB_SEC_MIC_LEN;
		parent_log.aps_fc = aux.frame_counter;
	}

	parent_log.aps_hdr = hdr;
	switch (hdr.type) {
	case ZZB_APS_FRAME_ACK:
		parent_log.aps_acks++;
		break;
	case ZZB_APS_FRAME_DATA:
		parent_log.aps_data++;
		if ((hdr.flags & ZZB_APS_ACK_REQ) && parent.withheld_acks > 0) {
			parent.withheld_acks--;
		} else if (hdr.flags & ZZB_APS_ACK_REQ) {
			send_aps_ack(&hdr);
		}

		if (hdr.profile == 0x0000 && hdr.dst_ep == 0) {
			rx_zdo(hdr.cluster, frame + off, len - off, dst);
		} else {
			zassert_true(len - off <= sizeof(parent_log.zcl));
			parent_log.zcl_frames++;
			parent_log.zcl_cluster = hdr.cluster;
			parent_log.zcl_src_ep = hdr.src_ep;
			parent_log.zcl_dst_ep = hdr.dst_ep;
			memcpy(parent_log.zcl, frame + off, len - off);
			parent_log.zcl_len = len - off;
			reply_zcl(&hdr, frame + off, len - off);
		}
		break;
	case ZZB_APS_FRAME_CMD:
		zassert_true(zzb_aps_cmd_parse(frame + off, len - off, &cmd) > 0);
		parent_log.aps_cmd = cmd;
		reply_aps_cmd(&cmd);
		break;
	}
}

static void reply(const struct zzb_nwk_hdr *hdr, const struct zzb_nwk_cmd *cmd, bool secure)
{
	struct zzb_nwk_cmd rsp;

	switch (cmd->id) {
	case ZZB_NWK_CMD_ED_TIMEOUT_REQ:
		parent_log.ed_timeout_reqs++;
		rsp = (struct zzb_nwk_cmd){
			.id = ZZB_NWK_CMD_ED_TIMEOUT_RSP,
			.ed_timeout_rsp = {.parent_info = parent.parent_info},
		};
		send_nwk_cmd(&rsp, true);
		break;
	case ZZB_NWK_CMD_REJOIN_REQ:
		/* The child keeps its address (R22 3.6.1.4.2); the security of the request is kept
		 */
		parent_log.rejoin_reqs++;
		parent_log.rejoin_hdr = *hdr;
		parent.child_addr = hdr->src;
		parent.child_eui64 = hdr->src_ext;
		parent.child_sleepy = !(cmd->capability & ZZB_NWK_CAP_RX_ON_IDLE);
		rsp = (struct zzb_nwk_cmd){
			.id = ZZB_NWK_CMD_REJOIN_RSP,
			.rejoin_rsp = {.short_addr = hdr->src},
		};
		send_nwk_cmd(&rsp, secure);
		if (!secure) {
			schedule_key();
		}
		break;
	case ZZB_NWK_CMD_LEAVE:
		parent_log.leaves++;
		break;
	default:
		break;
	}
}

/* Unsecures frame in place and handles the NWK command or APS frame in it */
static void rx_nwk(uint8_t *frame, size_t len)
{
	struct zzb_sec_aux_hdr aux;
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;
	int hdr_len = zzb_nwk_hdr_parse(frame, len, &hdr);
	size_t off = hdr_len;
	psa_key_id_t id;

	zassert_true(hdr_len > 0);
	if (hdr.flags & ZZB_NWK_SECURITY) {
		off += zzb_sec_aux_hdr_parse(frame + hdr_len, len - hdr_len, &aux);
		zassert_ok(zzb_sec_key_get(aux.key_seq == 1 ? nwk_key_1 : nwk_key, &id));
		zassert_ok(zzb_sec_frame_decrypt(id, aux.src_addr, frame, hdr_len, len));
		len -= ZZB_SEC_MIC_LEN;
		parent_log.nwk_key_seq = aux.key_seq;
	}

	if (hdr.type == ZZB_NWK_FRAME_DATA) {
		rx_aps(frame + off, len - off, hdr.dst);
		return;
	}

	zassert_true(zzb_nwk_cmd_parse(frame + off, len - off, &cmd) > 0);
	parent_log.hdr = hdr;
	parent_log.cmd = cmd;
	reply(&hdr, &cmd, hdr.flags & ZZB_NWK_SECURITY);
}

/* Runs inside the tx() of the child, so the replies reach it after the confirm */
static bool rx_cb(const uint8_t *data, size_t len)
{
	uint8_t frame[ZZB_BUF_SIZE];
	struct zzb_mac_hdr hdr;
	struct zzb_mac_cmd cmd;
	int hdr_len;

	/* The child sends the same buffer again after a missing ACK */
	memcpy(frame, data, len);
	hdr_len = zzb_mac_hdr_parse(frame, len, &hdr);
	zassert_true(hdr_len > 0);

	if (hdr.type == ZZB_MAC_FRAME_DATA) {
		rx_nwk(frame + hdr_len, len - hdr_len);
		return false;
	}

	zassert_true(zzb_mac_cmd_parse(frame + hdr_len, len - hdr_len, &cmd) > 0);
	switch (cmd.id) {
	case ZZB_MAC_CMD_BEACON_REQ:
		parent_log.beacon_reqs++;
		send_beacon();
		return false;
	case ZZB_MAC_CMD_ASSOC_REQ:
		parent.child_eui64 = hdr.src.ext_addr;
		parent.child_sleepy = !(cmd.capability & ZZB_NWK_CAP_RX_ON_IDLE);
		parent.assoc_pending = true;
		return false;
	case ZZB_MAC_CMD_DATA_REQ:
		if (parent.assoc_pending) {
			parent.assoc_pending = false;
			send_assoc_rsp();
			schedule_key();
			return true;
		}

		parent_log.polls++;
		return send_queued();
	default:
		return false;
	}
}

static void send_network_key(void);

static void key_handler(struct k_work *work)
{
	send_network_key();
}

static void set_filter(enum ieee802154_filter_type type, struct ieee802154_filter filter)
{
	const struct ieee802154_radio_api *api = fake_radios[0]->api;

	zassert_ok(api->filter(fake_radios[0], true, type, &filter));
}

void parent_start(uint16_t short_addr, uint64_t eui64, uint16_t child_addr, uint8_t parent_info)
{
	const struct ieee802154_radio_api *api = fake_radios[0]->api;
	uint8_t ieee_addr[8];

	parent.short_addr = short_addr;
	parent.eui64 = eui64;
	parent.child_addr = child_addr;
	parent.parent_info = parent_info;
	parent.assoc_pending = false;
	parent.link_key = zzb_aps_default_tc_link_key;
	parent.withheld_acks = 0;
	parent.stack_revision = 22;
	parent.key_after_join = false;
	parent.child_sleepy = false;
	queue_len = 0;
	memset(&parent_log, 0, sizeof(parent_log));
	/* Safe on the zeroed work item of the first call */
	k_work_cancel_delayable(&key_work);
	k_work_init_delayable(&key_work, key_handler);

	sys_put_le64(eui64, ieee_addr);
	set_filter(IEEE802154_FILTER_TYPE_IEEE_ADDR,
		   (struct ieee802154_filter){.ieee_addr = ieee_addr});
	set_filter(IEEE802154_FILTER_TYPE_SHORT_ADDR,
		   (struct ieee802154_filter){.short_addr = short_addr});
	set_filter(IEEE802154_FILTER_TYPE_PAN_ID, (struct ieee802154_filter){.pan_id = PAN_ID});
	zassert_ok(api->set_channel(fake_radios[0], CHANNEL));
	fake_radio_set_rx_cb(0, rx_cb);
}

struct leave_call {
	struct k_work work;
	bool rejoin;
};

/* The key ID of zzb_sec_key_get() is valid on the stack thread only */
static void leave_handler(struct k_work *work)
{
	struct leave_call *call = CONTAINER_OF(work, struct leave_call, work);
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_LEAVE,
		.leave_options = ZZB_NWK_LEAVE_REQUEST | (call->rejoin ? ZZB_NWK_LEAVE_REJOIN : 0),
	};

	send_nwk_cmd(&cmd, true);
}

void parent_send_leave_request(bool rejoin)
{
	struct leave_call call = {
		.rejoin = rejoin,
	};

	zzb_call_sync(&call.work, leave_handler);
}

static struct zzb_aps_cmd nwk_key_cmd(const uint8_t *key, uint8_t seq)
{
	struct zzb_aps_cmd cmd = {
		.id = ZZB_APS_CMD_TRANSPORT_KEY,
		.transport_key.type = ZZB_APS_KEY_NWK,
		.transport_key.seq = seq,
		.transport_key.dst = parent.child_eui64,
		.transport_key.src = parent.eui64,
	};

	memcpy(cmd.transport_key.key, key, ZZB_SEC_KEY_LEN);
	return cmd;
}

/* R22 4.6.3.2.3: the network key under the key-transport key, without NWK security */
static void send_network_key(void)
{
	const struct zzb_aps_cmd cmd = nwk_key_cmd(nwk_key, 0);

	send_aps_cmd(&cmd, parent.link_key, ZZB_SEC_KEY_TRANSPORT, parent.child_addr, false);
}

static void transport_key_handler(struct k_work *work)
{
	send_network_key();
}

void parent_send_transport_key(void)
{
	struct k_work work;

	zzb_call_sync(&work, transport_key_handler);
}

void parent_withhold_aps_acks(int count)
{
	parent.withheld_acks = count;
}

struct data_call {
	struct k_work work;
	uint16_t group;
	uint16_t cluster;
	const uint8_t *asdu;
	size_t len;
	uint8_t counter;
};

static void aps_data_handler(struct k_work *work)
{
	struct data_call *call = CONTAINER_OF(work, struct data_call, work);

	send_zcl(0x01, call->cluster, call->asdu, call->len, call->counter);
}

void parent_send_aps_data(uint8_t counter)
{
	struct data_call call = {
		.cluster = 0x0006,
		.asdu = asdu,
		.len = sizeof(asdu),
		.counter = counter,
	};

	zzb_call_sync(&call.work, aps_data_handler);
}

void parent_send_zcl(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	struct data_call call = {
		.cluster = cluster,
		.asdu = asdu,
		.len = len,
		.counter = parent.aps_counter++,
	};

	zzb_call_sync(&call.work, aps_data_handler);
}

static void group_zcl_handler(struct k_work *work)
{
	struct data_call *call = CONTAINER_OF(work, struct data_call, work);
	const struct zzb_aps_hdr hdr = {
		.type = ZZB_APS_FRAME_DATA,
		.delivery = ZZB_APS_DELIVERY_GROUP,
		.group = call->group,
		.cluster = call->cluster,
		.profile = 0x0104,
		.src_ep = 0x01,
		.counter = parent.aps_counter++,
	};
	uint8_t payload[ZZB_BUF_SIZE];

	send_nwk(ZZB_NWK_FRAME_DATA, ZZB_NWK_SECURITY, 0xfffd, payload,
		 write_aps(payload, hdr, call->asdu, call->len, NULL, ZZB_SEC_KEY_DATA));
}

void parent_send_group_zcl(uint16_t group, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	struct data_call call = {
		.group = group,
		.cluster = cluster,
		.asdu = asdu,
		.len = len,
	};

	zzb_call_sync(&call.work, group_zcl_handler);
}

static void zdo_handler(struct k_work *work)
{
	struct data_call *call = CONTAINER_OF(work, struct data_call, work);

	send_zdo(call->cluster, call->asdu, call->len);
}

void parent_send_zdo(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	struct data_call call = {
		.cluster = cluster,
		.asdu = asdu,
		.len = len,
	};

	zzb_call_sync(&call.work, zdo_handler);
}

/* R22 4.6.3.4.1: a unicast key update, then a broadcast Switch Key with NWK security only */
static void nwk_key_update_handler(struct k_work *work)
{
	const struct zzb_aps_cmd key = nwk_key_cmd(nwk_key_1, 1);
	const struct zzb_aps_cmd switch_key = {
		.id = ZZB_APS_CMD_SWITCH_KEY,
		.switch_key_seq = 1,
	};

	send_aps_cmd(&key, parent.link_key, ZZB_SEC_KEY_TRANSPORT, parent.child_addr, true);
	send_aps_cmd(&switch_key, NULL, ZZB_SEC_KEY_DATA, 0xfffd, true);
}

void parent_send_nwk_key_update(void)
{
	struct k_work work;

	zzb_call_sync(&work, nwk_key_update_handler);
}

void parent_set_link_key(const uint8_t *key)
{
	parent.link_key = key;
}

void parent_set_stack_revision(uint8_t revision)
{
	parent.stack_revision = revision;
}

void parent_send_key_after_join(bool send)
{
	parent.key_after_join = send;
}

struct mgmt_leave_call {
	struct k_work work;
	bool rejoin;
};

/* R22 2.4.3.3.5: the device address of the child, rejoin in bit 7 */
static void mgmt_leave_handler(struct k_work *work)
{
	struct mgmt_leave_call *call = CONTAINER_OF(work, struct mgmt_leave_call, work);
	uint8_t req[10] = {0x45};

	sys_put_le64(parent.child_eui64, req + 1);
	req[9] = call->rejoin ? BIT(7) : 0;
	send_zdo(MGMT_LEAVE_REQ, req, sizeof(req));
}

void parent_send_mgmt_leave(bool rejoin)
{
	struct mgmt_leave_call call = {
		.rejoin = rejoin,
	};

	zzb_call_sync(&call.work, mgmt_leave_handler);
}

void parent_set_channel(uint8_t channel)
{
	const struct ieee802154_radio_api *api = fake_radios[0]->api;

	zassert_ok(api->set_channel(fake_radios[0], channel));
}
