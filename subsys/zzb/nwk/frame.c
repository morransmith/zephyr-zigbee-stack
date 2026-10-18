/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
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
