/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>

#include "mac/frame.h"

/* Frame Control field */
#define FCF_TYPE        GENMASK(2, 0)
#define FCF_SECURITY    BIT(3)
#define FCF_PENDING     BIT(4)
#define FCF_ACK_REQ     BIT(5)
#define FCF_PAN_ID_COMP BIT(6)
#define FCF_DST_MODE    GENMASK(11, 10)
#define FCF_VERSION     GENMASK(13, 12)
#define FCF_SRC_MODE    GENMASK(15, 14)

/* GTS Specification and Pending Address Specification fields */
#define GTS_COUNT     GENMASK(2, 0)
#define PENDING_SHORT GENMASK(2, 0)
#define PENDING_EXT   GENMASK(6, 4)

static size_t addr_len(const struct zzb_mac_addr *addr, bool pan_id)
{
	if (addr->mode == ZZB_MAC_ADDR_NONE) {
		return 0;
	}

	return (pan_id ? 2 : 0) + (addr->mode == ZZB_MAC_ADDR_SHORT ? 2 : 8);
}

/* comp is set only when both addresses are present (802.15.4 5.2.1.1.5) */
static size_t hdr_len(const struct zzb_mac_hdr *hdr, bool comp)
{
	return 3 + addr_len(&hdr->dst, true) + addr_len(&hdr->src, !comp);
}

static bool pan_id_comp(const struct zzb_mac_hdr *hdr)
{
	return hdr->dst.mode != ZZB_MAC_ADDR_NONE && hdr->src.mode != ZZB_MAC_ADDR_NONE &&
	       hdr->dst.pan_id == hdr->src.pan_id;
}

static uint8_t *put_addr(uint8_t *p, const struct zzb_mac_addr *addr, bool pan_id)
{
	if (addr->mode == ZZB_MAC_ADDR_NONE) {
		return p;
	}

	if (pan_id) {
		sys_put_le16(addr->pan_id, p);
		p += 2;
	}

	if (addr->mode == ZZB_MAC_ADDR_SHORT) {
		sys_put_le16(addr->short_addr, p);
		return p + 2;
	}

	sys_put_le64(addr->ext_addr, p);
	return p + 8;
}

static const uint8_t *get_addr(const uint8_t *p, struct zzb_mac_addr *addr, bool pan_id)
{
	if (addr->mode == ZZB_MAC_ADDR_NONE) {
		return p;
	}

	if (pan_id) {
		addr->pan_id = sys_get_le16(p);
		p += 2;
	}

	if (addr->mode == ZZB_MAC_ADDR_SHORT) {
		addr->short_addr = sys_get_le16(p);
		return p + 2;
	}

	addr->ext_addr = sys_get_le64(p);
	return p + 8;
}

size_t zzb_mac_hdr_len(const struct zzb_mac_hdr *hdr)
{
	return hdr_len(hdr, pan_id_comp(hdr));
}

size_t zzb_mac_hdr_write(const struct zzb_mac_hdr *hdr, uint8_t *p)
{
	bool comp = pan_id_comp(hdr);
	uint16_t fcf = FIELD_PREP(FCF_TYPE, hdr->type) |
		       ((hdr->flags & ZZB_FRAME_PENDING) ? FCF_PENDING : 0) |
		       ((hdr->flags & ZZB_FRAME_ACK_REQ) ? FCF_ACK_REQ : 0) |
		       (comp ? FCF_PAN_ID_COMP : 0) | FIELD_PREP(FCF_DST_MODE, hdr->dst.mode) |
		       FIELD_PREP(FCF_SRC_MODE, hdr->src.mode);

	sys_put_le16(fcf, p);
	p[2] = hdr->seq;
	p = put_addr(p + 3, &hdr->dst, true);
	put_addr(p, &hdr->src, !comp);

	return hdr_len(hdr, comp);
}

int zzb_mac_hdr_parse(const uint8_t *p, size_t len, struct zzb_mac_hdr *hdr)
{
	uint16_t fcf;
	bool comp;

	if (len < 3) {
		return -EINVAL;
	}

	fcf = sys_get_le16(p);
	/* Frame types 4 to 7 and addressing mode 1 are reserved */
	if (FIELD_GET(FCF_TYPE, fcf) > ZZB_MAC_FRAME_CMD || FIELD_GET(FCF_DST_MODE, fcf) == 1 ||
	    FIELD_GET(FCF_SRC_MODE, fcf) == 1) {
		return -EINVAL;
	}

	if (FIELD_GET(FCF_VERSION, fcf) > 1 || (fcf & FCF_SECURITY)) {
		return -ENOTSUP;
	}

	*hdr = (struct zzb_mac_hdr){
		.type = FIELD_GET(FCF_TYPE, fcf),
		.flags = ((fcf & FCF_PENDING) ? ZZB_FRAME_PENDING : 0) |
			 ((fcf & FCF_ACK_REQ) ? ZZB_FRAME_ACK_REQ : 0),
		.seq = p[2],
		.dst.mode = FIELD_GET(FCF_DST_MODE, fcf),
		.src.mode = FIELD_GET(FCF_SRC_MODE, fcf),
	};
	comp = (fcf & FCF_PAN_ID_COMP) && hdr->dst.mode != ZZB_MAC_ADDR_NONE &&
	       hdr->src.mode != ZZB_MAC_ADDR_NONE;

	if (len < hdr_len(hdr, comp)) {
		return -EINVAL;
	}

	p = get_addr(p + 3, &hdr->dst, true);
	get_addr(p, &hdr->src, !comp);
	if (comp) {
		hdr->src.pan_id = hdr->dst.pan_id;
	}

	return hdr_len(hdr, comp);
}

size_t zzb_mac_beacon_write(uint16_t superframe, uint8_t *p)
{
	sys_put_le16(superframe, p);
	p[2] = 0;
	p[3] = 0;

	return 4;
}

int zzb_mac_beacon_parse(const uint8_t *p, size_t len, uint16_t *superframe)
{
	size_t gts_count;
	size_t off;

	if (len < 3) {
		return -EINVAL;
	}

	*superframe = sys_get_le16(p);
	gts_count = FIELD_GET(GTS_COUNT, p[2]);
	/* The GTS Directions field is present only with GTS descriptors */
	off = 3 + (gts_count > 0 ? 1 + 3 * gts_count : 0);
	if (len < off + 1) {
		return -EINVAL;
	}

	off += 1 + 2 * FIELD_GET(PENDING_SHORT, p[off]) + 8 * FIELD_GET(PENDING_EXT, p[off]);
	if (len < off) {
		return -EINVAL;
	}

	return off;
}

/* Returns the length of the command fields after the identifier, or -ENOTSUP */
static int cmd_payload_len(uint8_t id)
{
	switch (id) {
	case ZZB_MAC_CMD_DATA_REQ:
	case ZZB_MAC_CMD_ORPHAN:
	case ZZB_MAC_CMD_BEACON_REQ:
		return 0;
	case ZZB_MAC_CMD_ASSOC_REQ:
		return 1;
	case ZZB_MAC_CMD_ASSOC_RSP:
		return 3;
	case ZZB_MAC_CMD_COORD_REALIGN:
		return 7;
	default:
		return -ENOTSUP;
	}
}

size_t zzb_mac_cmd_write(const struct zzb_mac_cmd *cmd, uint8_t *p)
{
	p[0] = cmd->id;

	switch (cmd->id) {
	case ZZB_MAC_CMD_ASSOC_REQ:
		p[1] = cmd->capability;
		break;
	case ZZB_MAC_CMD_ASSOC_RSP:
		sys_put_le16(cmd->assoc_rsp.short_addr, p + 1);
		p[3] = cmd->assoc_rsp.status;
		break;
	case ZZB_MAC_CMD_COORD_REALIGN:
		sys_put_le16(cmd->realign.pan_id, p + 1);
		sys_put_le16(cmd->realign.coord_short_addr, p + 3);
		p[5] = cmd->realign.channel;
		sys_put_le16(cmd->realign.short_addr, p + 6);
		break;
	default:
		break;
	}

	return 1 + cmd_payload_len(cmd->id);
}

int zzb_mac_cmd_parse(const uint8_t *p, size_t len, struct zzb_mac_cmd *cmd)
{
	int payload_len;

	if (len < 1) {
		return -EINVAL;
	}

	payload_len = cmd_payload_len(p[0]);
	if (payload_len < 0) {
		return payload_len;
	}

	if (len < 1 + payload_len) {
		return -EINVAL;
	}

	*cmd = (struct zzb_mac_cmd){
		.id = p[0],
	};

	switch (cmd->id) {
	case ZZB_MAC_CMD_ASSOC_REQ:
		cmd->capability = p[1];
		break;
	case ZZB_MAC_CMD_ASSOC_RSP:
		cmd->assoc_rsp.short_addr = sys_get_le16(p + 1);
		cmd->assoc_rsp.status = p[3];
		break;
	case ZZB_MAC_CMD_COORD_REALIGN:
		cmd->realign.pan_id = sys_get_le16(p + 1);
		cmd->realign.coord_short_addr = sys_get_le16(p + 3);
		cmd->realign.channel = p[5];
		cmd->realign.short_addr = sys_get_le16(p + 6);
		break;
	default:
		break;
	}

	return 1 + payload_len;
}
