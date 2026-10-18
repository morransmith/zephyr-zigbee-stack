/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "common/queue.h"
#include "fake_radio.h"
#include "mac/frame.h"
#include "nwk/frame.h"
#include "parent.h"
#include "sec/aux_hdr.h"
#include "sec/ccm.h"
#include "sec/key_cache.h"

/* A coordinator that permits association */
#define SUPERFRAME (0x0fff | ZZB_MAC_SF_PAN_COORD | ZZB_MAC_SF_ASSOC_PERMIT)

const uint8_t nwk_key[ZZB_SEC_KEY_LEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

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
} parent;

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

static void send_mac(struct zzb_mac_hdr *hdr, const uint8_t *payload, size_t len)
{
	uint8_t frame[ZZB_BUF_SIZE];
	size_t hdr_len;

	hdr->seq = parent.dsn++;
	hdr_len = zzb_mac_hdr_write(hdr, frame);
	memcpy(frame + hdr_len, payload, len);
	fake_radio_send(0, frame, hdr_len + len);
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

/* Sends cmd to the child with both IEEE addresses and radius 1 */
static void send_nwk_cmd(const struct zzb_nwk_cmd *cmd, bool secure)
{
	struct zzb_mac_hdr mac = {
		.type = ZZB_MAC_FRAME_DATA,
		.flags = ZZB_FRAME_ACK_REQ,
		.dst = short_addr(parent.child_addr),
		.src = short_addr(parent.short_addr),
	};
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE | (secure ? ZZB_NWK_SECURITY : 0),
		.dst = parent.child_addr,
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
	uint8_t frame[ZZB_BUF_SIZE];
	size_t hdr_len = zzb_nwk_hdr_write(&hdr, frame);
	size_t len = hdr_len;
	psa_key_id_t id;

	if (secure) {
		len += zzb_sec_aux_hdr_write(&aux, frame + len);
	}

	len += zzb_nwk_cmd_write(cmd, frame + len);
	if (secure) {
		zassert_ok(zzb_sec_key_get(nwk_key, &id));
		zassert_ok(zzb_sec_frame_encrypt(id, parent.eui64, frame, hdr_len, len));
		len += ZZB_SEC_MIC_LEN;
	}

	send_mac(&mac, frame, len);
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
		rsp = (struct zzb_nwk_cmd){
			.id = ZZB_NWK_CMD_REJOIN_RSP,
			.rejoin_rsp = {.short_addr = hdr->src},
		};
		send_nwk_cmd(&rsp, secure);
		break;
	case ZZB_NWK_CMD_LEAVE:
		parent_log.leaves++;
		break;
	default:
		break;
	}
}

/* Unsecures frame in place and handles the NWK command in it */
static void rx_nwk(uint8_t *frame, size_t len)
{
	struct zzb_sec_aux_hdr aux;
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;
	int hdr_len = zzb_nwk_hdr_parse(frame, len, &hdr);
	size_t off = hdr_len;
	psa_key_id_t id;

	zassert_true(hdr_len > 0);
	if (hdr.type != ZZB_NWK_FRAME_CMD) {
		return;
	}

	if (hdr.flags & ZZB_NWK_SECURITY) {
		off += zzb_sec_aux_hdr_parse(frame + hdr_len, len - hdr_len, &aux);
		zassert_ok(zzb_sec_key_get(nwk_key, &id));
		zassert_ok(zzb_sec_frame_decrypt(id, aux.src_addr, frame, hdr_len, len));
		len -= ZZB_SEC_MIC_LEN;
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
		parent.assoc_pending = true;
		return false;
	case ZZB_MAC_CMD_DATA_REQ:
		if (parent.assoc_pending) {
			parent.assoc_pending = false;
			send_assoc_rsp();
			return true;
		}

		parent_log.polls++;
		return false;
	default:
		return false;
	}
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
	memset(&parent_log, 0, sizeof(parent_log));

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
