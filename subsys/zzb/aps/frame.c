/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "aps/frame.h"

/* Frame control field */
#define FCF_TYPE     GENMASK(1, 0)
#define FCF_DELIVERY GENMASK(3, 2)
#define FCF_FLAGS    (ZZB_APS_ACK_FORMAT | ZZB_APS_SECURITY | ZZB_APS_ACK_REQ | ZZB_APS_EXT_HDR)

#define FRAME_TYPE_INTER_PAN 3
#define DELIVERY_RESERVED    1

/* Extended frame control field: fragmentation in bits 0-1, the other bits reserved */
#define EXT_FCF_FRAGMENTATION GENMASK(1, 0)

static bool has_dst_ep(const struct zzb_aps_hdr *hdr)
{
	if (hdr->type == ZZB_APS_FRAME_DATA) {
		return hdr->delivery != ZZB_APS_DELIVERY_GROUP;
	}

	return hdr->type == ZZB_APS_FRAME_ACK && !(hdr->flags & ZZB_APS_ACK_FORMAT);
}

static bool has_group(const struct zzb_aps_hdr *hdr)
{
	return hdr->type == ZZB_APS_FRAME_DATA && hdr->delivery == ZZB_APS_DELIVERY_GROUP;
}

/* The cluster, profile and source endpoint */
static bool has_cluster(const struct zzb_aps_hdr *hdr)
{
	return hdr->type == ZZB_APS_FRAME_DATA ||
	       (hdr->type == ZZB_APS_FRAME_ACK && !(hdr->flags & ZZB_APS_ACK_FORMAT));
}

size_t zzb_aps_hdr_len(const struct zzb_aps_hdr *hdr)
{
	return 2 + (has_dst_ep(hdr) ? 1 : 0) + (has_group(hdr) ? 2 : 0) +
	       (has_cluster(hdr) ? 5 : 0) + ((hdr->flags & ZZB_APS_EXT_HDR) ? 1 : 0);
}

size_t zzb_aps_hdr_write(const struct zzb_aps_hdr *hdr, uint8_t *p)
{
	uint8_t *q = p + 1;

	p[0] = FIELD_PREP(FCF_TYPE, hdr->type) | FIELD_PREP(FCF_DELIVERY, hdr->delivery) |
	       (hdr->flags & FCF_FLAGS);

	if (has_dst_ep(hdr)) {
		*q++ = hdr->dst_ep;
	}

	if (has_group(hdr)) {
		sys_put_le16(hdr->group, q);
		q += 2;
	}

	if (has_cluster(hdr)) {
		sys_put_le16(hdr->cluster, q);
		sys_put_le16(hdr->profile, q + 2);
		q[4] = hdr->src_ep;
		q += 5;
	}

	*q++ = hdr->counter;

	if (hdr->flags & ZZB_APS_EXT_HDR) {
		*q = 0;
	}

	return zzb_aps_hdr_len(hdr);
}

int zzb_aps_hdr_parse(const uint8_t *p, size_t len, struct zzb_aps_hdr *hdr)
{
	const uint8_t *q = p + 1;

	if (len < 1) {
		return -EINVAL;
	}

	if (FIELD_GET(FCF_TYPE, p[0]) == FRAME_TYPE_INTER_PAN) {
		return -ENOTSUP;
	}

	if (FIELD_GET(FCF_DELIVERY, p[0]) == DELIVERY_RESERVED) {
		return -EINVAL;
	}

	*hdr = (struct zzb_aps_hdr){
		.type = FIELD_GET(FCF_TYPE, p[0]),
		.delivery = FIELD_GET(FCF_DELIVERY, p[0]),
		.flags = p[0] & FCF_FLAGS,
	};

	if (len < zzb_aps_hdr_len(hdr)) {
		return -EINVAL;
	}

	if (has_dst_ep(hdr)) {
		hdr->dst_ep = *q++;
	}

	if (has_group(hdr)) {
		hdr->group = sys_get_le16(q);
		q += 2;
	}

	if (has_cluster(hdr)) {
		hdr->cluster = sys_get_le16(q);
		hdr->profile = sys_get_le16(q + 2);
		hdr->src_ep = q[4];
		q += 5;
	}

	hdr->counter = *q++;

	if (!(hdr->flags & ZZB_APS_EXT_HDR)) {
		return zzb_aps_hdr_len(hdr);
	}

	if (*q & EXT_FCF_FRAGMENTATION) {
		return -ENOTSUP;
	}

	if (*q != 0) {
		return -EINVAL;
	}

	return zzb_aps_hdr_len(hdr);
}

/* Length of the key type and the key descriptor (R22 4.4.10.1.3), or -ENOTSUP */
static int transport_key_len(uint8_t type)
{
	switch (type) {
	case ZZB_APS_KEY_NWK:
		return 1 + ZZB_SEC_KEY_LEN + 1 + 8 + 8;
	case ZZB_APS_KEY_APP_LINK:
		return 1 + ZZB_SEC_KEY_LEN + 8 + 1;
	case ZZB_APS_KEY_TC_LINK:
		return 1 + ZZB_SEC_KEY_LEN + 8 + 8;
	default:
		return -ENOTSUP;
	}
}

static int request_key_len(uint8_t type)
{
	switch (type) {
	case ZZB_APS_REQUEST_APP_LINK:
		return 1 + 8;
	case ZZB_APS_REQUEST_TC_LINK:
		return 1;
	default:
		return -ENOTSUP;
	}
}

/* Payload length; p points to the payload of a received command with at least one octet */
static int payload_len(uint8_t id, const uint8_t *p)
{
	switch (id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		return transport_key_len(p[0]);
	case ZZB_APS_CMD_REQUEST_KEY:
		return request_key_len(p[0]);
	case ZZB_APS_CMD_SWITCH_KEY:
		return 1;
	case ZZB_APS_CMD_VERIFY_KEY:
		return 1 + 8 + ZZB_SEC_KEY_LEN;
	case ZZB_APS_CMD_CONFIRM_KEY:
		return 1 + 1 + 8;
	default:
		return -ENOTSUP;
	}
}

static size_t write_transport_key(const struct zzb_aps_cmd *cmd, uint8_t *q)
{
	q[0] = cmd->transport_key.type;
	memcpy(q + 1, cmd->transport_key.key, ZZB_SEC_KEY_LEN);
	q += 1 + ZZB_SEC_KEY_LEN;

	switch (cmd->transport_key.type) {
	case ZZB_APS_KEY_NWK:
		*q++ = cmd->transport_key.seq;
		__fallthrough;
	case ZZB_APS_KEY_TC_LINK:
		sys_put_le64(cmd->transport_key.dst, q);
		sys_put_le64(cmd->transport_key.src, q + 8);
		break;
	case ZZB_APS_KEY_APP_LINK:
		sys_put_le64(cmd->transport_key.partner, q);
		q[8] = cmd->transport_key.initiator;
		break;
	}

	return transport_key_len(cmd->transport_key.type);
}

size_t zzb_aps_cmd_write(const struct zzb_aps_cmd *cmd, uint8_t *p)
{
	uint8_t *q = p + 1;

	p[0] = cmd->id;

	switch (cmd->id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		return 1 + write_transport_key(cmd, q);
	case ZZB_APS_CMD_REQUEST_KEY:
		q[0] = cmd->request_key.type;
		if (cmd->request_key.type == ZZB_APS_REQUEST_APP_LINK) {
			sys_put_le64(cmd->request_key.partner, q + 1);
		}
		return 1 + request_key_len(cmd->request_key.type);
	case ZZB_APS_CMD_SWITCH_KEY:
		q[0] = cmd->switch_key_seq;
		return 2;
	case ZZB_APS_CMD_VERIFY_KEY:
		q[0] = cmd->verify_key.type;
		sys_put_le64(cmd->verify_key.src, q + 1);
		memcpy(q + 9, cmd->verify_key.hash, ZZB_SEC_KEY_LEN);
		return 1 + 9 + ZZB_SEC_KEY_LEN;
	case ZZB_APS_CMD_CONFIRM_KEY:
		q[0] = cmd->confirm_key.status;
		q[1] = cmd->confirm_key.type;
		sys_put_le64(cmd->confirm_key.dst, q + 2);
		return 1 + 10;
	}

	return 1;
}

static void parse_transport_key(const uint8_t *q, struct zzb_aps_cmd *cmd)
{
	cmd->transport_key.type = q[0];
	memcpy(cmd->transport_key.key, q + 1, ZZB_SEC_KEY_LEN);
	q += 1 + ZZB_SEC_KEY_LEN;

	switch (cmd->transport_key.type) {
	case ZZB_APS_KEY_NWK:
		cmd->transport_key.seq = *q++;
		__fallthrough;
	case ZZB_APS_KEY_TC_LINK:
		cmd->transport_key.dst = sys_get_le64(q);
		cmd->transport_key.src = sys_get_le64(q + 8);
		break;
	case ZZB_APS_KEY_APP_LINK:
		cmd->transport_key.partner = sys_get_le64(q);
		cmd->transport_key.initiator = q[8];
		break;
	}
}

int zzb_aps_cmd_parse(const uint8_t *p, size_t len, struct zzb_aps_cmd *cmd)
{
	const uint8_t *q = p + 1;
	int plen;

	if (len < 2) {
		return -EINVAL;
	}

	plen = payload_len(p[0], q);
	if (plen < 0) {
		return plen;
	}

	if (len < 1 + plen) {
		return -EINVAL;
	}

	*cmd = (struct zzb_aps_cmd){
		.id = p[0],
	};

	switch (cmd->id) {
	case ZZB_APS_CMD_TRANSPORT_KEY:
		parse_transport_key(q, cmd);
		break;
	case ZZB_APS_CMD_REQUEST_KEY:
		cmd->request_key.type = q[0];
		if (cmd->request_key.type == ZZB_APS_REQUEST_APP_LINK) {
			cmd->request_key.partner = sys_get_le64(q + 1);
		}
		break;
	case ZZB_APS_CMD_SWITCH_KEY:
		cmd->switch_key_seq = q[0];
		break;
	case ZZB_APS_CMD_VERIFY_KEY:
		cmd->verify_key.type = q[0];
		cmd->verify_key.src = sys_get_le64(q + 1);
		memcpy(cmd->verify_key.hash, q + 9, ZZB_SEC_KEY_LEN);
		break;
	case ZZB_APS_CMD_CONFIRM_KEY:
		cmd->confirm_key.status = q[0];
		cmd->confirm_key.type = q[1];
		cmd->confirm_key.dst = sys_get_le64(q + 2);
		break;
	}

	return 1 + plen;
}
