/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "nwk/frame.h"

/* Frame control field */
#define FCF_TYPE    GENMASK(1, 0)
#define FCF_VERSION GENMASK(5, 2)
#define FCF_FLAGS                                                                                  \
	(ZZB_NWK_DISCOVER_ROUTE | ZZB_NWK_MULTICAST | ZZB_NWK_SECURITY | ZZB_NWK_SRC_ROUTE |       \
	 ZZB_NWK_DST_IEEE | ZZB_NWK_SRC_IEEE | ZZB_NWK_ED_INITIATOR)

#define FRAME_TYPE_RESERVED  2
#define FRAME_TYPE_INTER_PAN 3

size_t zzb_nwk_hdr_len(const struct zzb_nwk_hdr *hdr)
{
	return 8 + ((hdr->flags & ZZB_NWK_DST_IEEE) ? 8 : 0) +
	       ((hdr->flags & ZZB_NWK_SRC_IEEE) ? 8 : 0) +
	       ((hdr->flags & ZZB_NWK_MULTICAST) ? 1 : 0) +
	       ((hdr->flags & ZZB_NWK_SRC_ROUTE) ? 2 + 2 * hdr->relay_count : 0);
}

size_t zzb_nwk_hdr_write(const struct zzb_nwk_hdr *hdr, uint8_t *p)
{
	uint8_t *q = p + 8;

	sys_put_le16(FIELD_PREP(FCF_TYPE, hdr->type) |
			     FIELD_PREP(FCF_VERSION, ZZB_NWK_PROTOCOL_VERSION) |
			     (hdr->flags & FCF_FLAGS),
		     p);
	sys_put_le16(hdr->dst, p + 2);
	sys_put_le16(hdr->src, p + 4);
	p[6] = hdr->radius;
	p[7] = hdr->seq;

	if (hdr->flags & ZZB_NWK_DST_IEEE) {
		sys_put_le64(hdr->dst_ext, q);
		q += 8;
	}

	if (hdr->flags & ZZB_NWK_SRC_IEEE) {
		sys_put_le64(hdr->src_ext, q);
		q += 8;
	}

	if (hdr->flags & ZZB_NWK_MULTICAST) {
		*q++ = hdr->mcast_ctrl;
	}

	if (hdr->flags & ZZB_NWK_SRC_ROUTE) {
		*q++ = hdr->relay_count;
		*q++ = hdr->relay_index;
		for (int i = 0; i < hdr->relay_count; i++) {
			sys_put_le16(hdr->relays[i], q + 2 * i);
		}
	}

	return zzb_nwk_hdr_len(hdr);
}

int zzb_nwk_hdr_parse(const uint8_t *p, size_t len, struct zzb_nwk_hdr *hdr)
{
	const uint8_t *q = p + 8;
	uint16_t fcf;

	if (len < 2) {
		return -EINVAL;
	}

	fcf = sys_get_le16(p);
	if (FIELD_GET(FCF_TYPE, fcf) == FRAME_TYPE_RESERVED) {
		return -EINVAL;
	}

	if (FIELD_GET(FCF_TYPE, fcf) == FRAME_TYPE_INTER_PAN ||
	    FIELD_GET(FCF_VERSION, fcf) != ZZB_NWK_PROTOCOL_VERSION) {
		return -ENOTSUP;
	}

	*hdr = (struct zzb_nwk_hdr){
		.type = FIELD_GET(FCF_TYPE, fcf),
		.flags = fcf & FCF_FLAGS,
	};

	/* Up to the relay list, since relay_count is still 0 */
	if (len < zzb_nwk_hdr_len(hdr)) {
		return -EINVAL;
	}

	hdr->dst = sys_get_le16(p + 2);
	hdr->src = sys_get_le16(p + 4);
	hdr->radius = p[6];
	hdr->seq = p[7];

	if (hdr->flags & ZZB_NWK_DST_IEEE) {
		hdr->dst_ext = sys_get_le64(q);
		q += 8;
	}

	if (hdr->flags & ZZB_NWK_SRC_IEEE) {
		hdr->src_ext = sys_get_le64(q);
		q += 8;
	}

	if (hdr->flags & ZZB_NWK_MULTICAST) {
		hdr->mcast_ctrl = *q++;
	}

	if (!(hdr->flags & ZZB_NWK_SRC_ROUTE)) {
		return zzb_nwk_hdr_len(hdr);
	}

	if (q[0] > ZZB_NWK_MAX_RELAYS) {
		return -ENOTSUP;
	}

	hdr->relay_count = q[0];
	hdr->relay_index = q[1];
	if (len < zzb_nwk_hdr_len(hdr)) {
		return -EINVAL;
	}

	for (int i = 0; i < hdr->relay_count; i++) {
		hdr->relays[i] = sys_get_le16(q + 2 + 2 * i);
	}

	return zzb_nwk_hdr_len(hdr);
}

/* Command options fields that the struct does not hold as bits */
#define LINK_STATUS_COUNT GENMASK(4, 0)
#define LINK_STATUS_FLAGS (ZZB_NWK_LINK_STATUS_FIRST_FRAME | ZZB_NWK_LINK_STATUS_LAST_FRAME)
#define REPORT_COUNT      GENMASK(4, 0)
#define REPORT_TYPE       GENMASK(7, 5)
#define POWER_DELTA_TYPE  GENMASK(1, 0)

/* Report and update type of the PAN identifier conflict report and update */
#define TYPE_PAN_ID 0

/* Payload length without optional fields and lists, or -ENOTSUP for a reserved identifier */
static int min_payload_len(uint8_t id)
{
	switch (id) {
	case ZZB_NWK_CMD_NWK_STATUS:
	case ZZB_NWK_CMD_LEAVE:
	case ZZB_NWK_CMD_ROUTE_RECORD:
	case ZZB_NWK_CMD_REJOIN_REQ:
	case ZZB_NWK_CMD_LINK_STATUS:
		return 1;
	case ZZB_NWK_CMD_ED_TIMEOUT_REQ:
	case ZZB_NWK_CMD_ED_TIMEOUT_RSP:
	case ZZB_NWK_CMD_LINK_POWER_DELTA:
		return 2;
	case ZZB_NWK_CMD_REJOIN_RSP:
		return 3;
	case ZZB_NWK_CMD_ROUTE_REQ:
		return 5;
	case ZZB_NWK_CMD_ROUTE_REPLY:
		return 7;
	case ZZB_NWK_CMD_NWK_REPORT:
		return 9;
	case ZZB_NWK_CMD_NWK_UPDATE:
		return 12;
	default:
		return -ENOTSUP;
	}
}

static size_t payload_len(const struct zzb_nwk_cmd *cmd)
{
	switch (cmd->id) {
	case ZZB_NWK_CMD_ROUTE_REQ:
		return 5 + ((cmd->route_req.options & ZZB_NWK_ROUTE_REQ_DST_IEEE) ? 8 : 0);
	case ZZB_NWK_CMD_ROUTE_REPLY:
		return 7 + ((cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_ORIG_IEEE) ? 8 : 0) +
		       ((cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_RESP_IEEE) ? 8 : 0);
	case ZZB_NWK_CMD_NWK_STATUS:
		return cmd->nwk_status.has_addr ? 3 : 1;
	case ZZB_NWK_CMD_ROUTE_RECORD:
		return 1 + 2 * cmd->route_record.count;
	case ZZB_NWK_CMD_LINK_STATUS:
		return 1 + ZZB_NWK_LINK_STATUS_ENTRY_LEN * cmd->link_status.count;
	case ZZB_NWK_CMD_NWK_REPORT:
		return 9 + 2 * cmd->nwk_report.count;
	case ZZB_NWK_CMD_LINK_POWER_DELTA:
		return 2 + ZZB_NWK_POWER_DELTA_ENTRY_LEN * cmd->power_delta.count;
	default:
		return min_payload_len(cmd->id);
	}
}

size_t zzb_nwk_cmd_write(const struct zzb_nwk_cmd *cmd, uint8_t *p)
{
	uint8_t *q = p + 1;
	uint8_t *ext;

	p[0] = cmd->id;

	switch (cmd->id) {
	case ZZB_NWK_CMD_ROUTE_REQ:
		q[0] = cmd->route_req.options;
		q[1] = cmd->route_req.id;
		sys_put_le16(cmd->route_req.dst, q + 2);
		q[4] = cmd->route_req.path_cost;
		if (cmd->route_req.options & ZZB_NWK_ROUTE_REQ_DST_IEEE) {
			sys_put_le64(cmd->route_req.dst_ext, q + 5);
		}
		break;
	case ZZB_NWK_CMD_ROUTE_REPLY:
		q[0] = cmd->route_reply.options;
		q[1] = cmd->route_reply.id;
		sys_put_le16(cmd->route_reply.orig, q + 2);
		sys_put_le16(cmd->route_reply.resp, q + 4);
		q[6] = cmd->route_reply.path_cost;
		ext = q + 7;
		if (cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_ORIG_IEEE) {
			sys_put_le64(cmd->route_reply.orig_ext, ext);
			ext += 8;
		}
		if (cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_RESP_IEEE) {
			sys_put_le64(cmd->route_reply.resp_ext, ext);
		}
		break;
	case ZZB_NWK_CMD_NWK_STATUS:
		q[0] = cmd->nwk_status.code;
		if (cmd->nwk_status.has_addr) {
			sys_put_le16(cmd->nwk_status.addr, q + 1);
		}
		break;
	case ZZB_NWK_CMD_LEAVE:
		q[0] = cmd->leave_options;
		break;
	case ZZB_NWK_CMD_ROUTE_RECORD:
		q[0] = cmd->route_record.count;
		memcpy(q + 1, cmd->route_record.relays, 2 * cmd->route_record.count);
		break;
	case ZZB_NWK_CMD_REJOIN_REQ:
		q[0] = cmd->capability;
		break;
	case ZZB_NWK_CMD_REJOIN_RSP:
		sys_put_le16(cmd->rejoin_rsp.short_addr, q);
		q[2] = cmd->rejoin_rsp.status;
		break;
	case ZZB_NWK_CMD_LINK_STATUS:
		q[0] = FIELD_PREP(LINK_STATUS_COUNT, cmd->link_status.count) |
		       (cmd->link_status.flags & LINK_STATUS_FLAGS);
		memcpy(q + 1, cmd->link_status.entries,
		       ZZB_NWK_LINK_STATUS_ENTRY_LEN * cmd->link_status.count);
		break;
	case ZZB_NWK_CMD_NWK_REPORT:
		q[0] = FIELD_PREP(REPORT_COUNT, cmd->nwk_report.count) |
		       FIELD_PREP(REPORT_TYPE, TYPE_PAN_ID);
		sys_put_le64(cmd->nwk_report.epid, q + 1);
		memcpy(q + 9, cmd->nwk_report.pan_ids, 2 * cmd->nwk_report.count);
		break;
	case ZZB_NWK_CMD_NWK_UPDATE:
		q[0] = FIELD_PREP(REPORT_COUNT, 1) | FIELD_PREP(REPORT_TYPE, TYPE_PAN_ID);
		sys_put_le64(cmd->nwk_update.epid, q + 1);
		q[9] = cmd->nwk_update.update_id;
		sys_put_le16(cmd->nwk_update.pan_id, q + 10);
		break;
	case ZZB_NWK_CMD_ED_TIMEOUT_REQ:
		q[0] = cmd->ed_timeout_req.timeout;
		q[1] = cmd->ed_timeout_req.config;
		break;
	case ZZB_NWK_CMD_ED_TIMEOUT_RSP:
		q[0] = cmd->ed_timeout_rsp.status;
		q[1] = cmd->ed_timeout_rsp.parent_info;
		break;
	case ZZB_NWK_CMD_LINK_POWER_DELTA:
		q[0] = FIELD_PREP(POWER_DELTA_TYPE, cmd->power_delta.type);
		q[1] = cmd->power_delta.count;
		memcpy(q + 2, cmd->power_delta.entries,
		       ZZB_NWK_POWER_DELTA_ENTRY_LEN * cmd->power_delta.count);
		break;
	}

	return 1 + payload_len(cmd);
}

int zzb_nwk_cmd_parse(const uint8_t *p, size_t len, struct zzb_nwk_cmd *cmd)
{
	const uint8_t *q = p + 1;
	const uint8_t *ext;
	int min_len;

	if (len < 1) {
		return -EINVAL;
	}

	min_len = min_payload_len(p[0]);
	if (min_len < 0) {
		return min_len;
	}

	if (len < 1 + min_len) {
		return -EINVAL;
	}

	*cmd = (struct zzb_nwk_cmd){
		.id = p[0],
	};

	switch (cmd->id) {
	case ZZB_NWK_CMD_ROUTE_REQ:
		cmd->route_req.options = q[0];
		cmd->route_req.id = q[1];
		cmd->route_req.dst = sys_get_le16(q + 2);
		cmd->route_req.path_cost = q[4];
		if (len < 1 + payload_len(cmd)) {
			return -EINVAL;
		}
		if (cmd->route_req.options & ZZB_NWK_ROUTE_REQ_DST_IEEE) {
			cmd->route_req.dst_ext = sys_get_le64(q + 5);
		}
		break;
	case ZZB_NWK_CMD_ROUTE_REPLY:
		cmd->route_reply.options = q[0];
		cmd->route_reply.id = q[1];
		cmd->route_reply.orig = sys_get_le16(q + 2);
		cmd->route_reply.resp = sys_get_le16(q + 4);
		cmd->route_reply.path_cost = q[6];
		if (len < 1 + payload_len(cmd)) {
			return -EINVAL;
		}
		ext = q + 7;
		if (cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_ORIG_IEEE) {
			cmd->route_reply.orig_ext = sys_get_le64(ext);
			ext += 8;
		}
		if (cmd->route_reply.options & ZZB_NWK_ROUTE_REPLY_RESP_IEEE) {
			cmd->route_reply.resp_ext = sys_get_le64(ext);
		}
		break;
	case ZZB_NWK_CMD_NWK_STATUS:
		cmd->nwk_status.code = q[0];
		cmd->nwk_status.has_addr = len >= 4;
		if (cmd->nwk_status.has_addr) {
			cmd->nwk_status.addr = sys_get_le16(q + 1);
		}
		break;
	case ZZB_NWK_CMD_LEAVE:
		cmd->leave_options = q[0];
		break;
	case ZZB_NWK_CMD_ROUTE_RECORD:
		cmd->route_record.count = q[0];
		cmd->route_record.relays = q + 1;
		break;
	case ZZB_NWK_CMD_REJOIN_REQ:
		cmd->capability = q[0];
		break;
	case ZZB_NWK_CMD_REJOIN_RSP:
		cmd->rejoin_rsp.short_addr = sys_get_le16(q);
		cmd->rejoin_rsp.status = q[2];
		break;
	case ZZB_NWK_CMD_LINK_STATUS:
		cmd->link_status.flags = q[0] & LINK_STATUS_FLAGS;
		cmd->link_status.count = FIELD_GET(LINK_STATUS_COUNT, q[0]);
		cmd->link_status.entries = q + 1;
		break;
	case ZZB_NWK_CMD_NWK_REPORT:
		if (FIELD_GET(REPORT_TYPE, q[0]) != TYPE_PAN_ID) {
			return -ENOTSUP;
		}
		cmd->nwk_report.count = FIELD_GET(REPORT_COUNT, q[0]);
		cmd->nwk_report.epid = sys_get_le64(q + 1);
		cmd->nwk_report.pan_ids = q + 9;
		break;
	case ZZB_NWK_CMD_NWK_UPDATE:
		if (FIELD_GET(REPORT_TYPE, q[0]) != TYPE_PAN_ID) {
			return -ENOTSUP;
		}
		if (FIELD_GET(REPORT_COUNT, q[0]) != 1) {
			return -EINVAL;
		}
		cmd->nwk_update.epid = sys_get_le64(q + 1);
		cmd->nwk_update.update_id = q[9];
		cmd->nwk_update.pan_id = sys_get_le16(q + 10);
		break;
	case ZZB_NWK_CMD_ED_TIMEOUT_REQ:
		cmd->ed_timeout_req.timeout = q[0];
		cmd->ed_timeout_req.config = q[1];
		break;
	case ZZB_NWK_CMD_ED_TIMEOUT_RSP:
		cmd->ed_timeout_rsp.status = q[0];
		cmd->ed_timeout_rsp.parent_info = q[1];
		break;
	case ZZB_NWK_CMD_LINK_POWER_DELTA:
		cmd->power_delta.type = FIELD_GET(POWER_DELTA_TYPE, q[0]);
		cmd->power_delta.count = q[1];
		cmd->power_delta.entries = q + 2;
		break;
	}

	/* The lists follow the fields read above */
	if (len < 1 + payload_len(cmd)) {
		return -EINVAL;
	}

	return 1 + payload_len(cmd);
}

/* Octets 1 and 2 of the beacon payload (R22 Figure 3-54) */
#define BEACON_STACK_PROFILE GENMASK(3, 0)
#define BEACON_VERSION       GENMASK(7, 4)
#define BEACON_ROUTER_CAP    BIT(2)
#define BEACON_DEPTH         GENMASK(6, 3)
#define BEACON_ED_CAP        BIT(7)

size_t zzb_nwk_beacon_write(const struct zzb_nwk_beacon *beacon, uint8_t *p)
{
	p[0] = 0;
	p[1] = FIELD_PREP(BEACON_STACK_PROFILE, beacon->stack_profile) |
	       FIELD_PREP(BEACON_VERSION, beacon->protocol_version);
	p[2] = (beacon->router_capacity ? BEACON_ROUTER_CAP : 0) |
	       FIELD_PREP(BEACON_DEPTH, beacon->depth) | (beacon->ed_capacity ? BEACON_ED_CAP : 0);
	sys_put_le64(beacon->ext_pan_id, p + 3);
	sys_put_le24(beacon->tx_offset, p + 11);
	p[14] = beacon->update_id;

	return ZZB_NWK_BEACON_LEN;
}

int zzb_nwk_beacon_parse(const uint8_t *p, size_t len, struct zzb_nwk_beacon *beacon)
{
	if (len < ZZB_NWK_BEACON_LEN) {
		return -EINVAL;
	}

	if (p[0] != 0) {
		return -ENOTSUP;
	}

	*beacon = (struct zzb_nwk_beacon){
		.stack_profile = FIELD_GET(BEACON_STACK_PROFILE, p[1]),
		.protocol_version = FIELD_GET(BEACON_VERSION, p[1]),
		.router_capacity = p[2] & BEACON_ROUTER_CAP,
		.depth = FIELD_GET(BEACON_DEPTH, p[2]),
		.ed_capacity = p[2] & BEACON_ED_CAP,
		.ext_pan_id = sys_get_le64(p + 3),
		.tx_offset = sys_get_le24(p + 11),
		.update_id = p[14],
	};

	return ZZB_NWK_BEACON_LEN;
}
