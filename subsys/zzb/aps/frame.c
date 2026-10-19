/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
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
