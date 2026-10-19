/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl.h>

#include "zcl/frame.h"

/* Frame control field */
#define FCF_TYPE  GENMASK(1, 0)
#define FCF_FLAGS (ZZB_ZCL_MANUFACTURER | ZZB_ZCL_TO_CLIENT | ZZB_ZCL_DISABLE_DEFAULT_RSP)

/* Strings with a 2-octet length field */
#define TYPE_OCTSTR16 0x43
#define TYPE_STRING16 0x44

size_t zzb_zcl_hdr_len(const struct zzb_zcl_hdr *hdr)
{
	return (hdr->flags & ZZB_ZCL_MANUFACTURER) ? 5 : 3;
}

size_t zzb_zcl_hdr_write(const struct zzb_zcl_hdr *hdr, uint8_t *p)
{
	uint8_t *q = p + 1;

	p[0] = FIELD_PREP(FCF_TYPE, hdr->type) | (hdr->flags & FCF_FLAGS);

	if (hdr->flags & ZZB_ZCL_MANUFACTURER) {
		sys_put_le16(hdr->manufacturer, q);
		q += 2;
	}

	q[0] = hdr->tsn;
	q[1] = hdr->cmd;
	return zzb_zcl_hdr_len(hdr);
}

int zzb_zcl_hdr_parse(const uint8_t *p, size_t len, struct zzb_zcl_hdr *hdr)
{
	const uint8_t *q = p + 1;

	if (len < 1) {
		return -EINVAL;
	}

	if (FIELD_GET(FCF_TYPE, p[0]) > ZZB_ZCL_FRAME_CLUSTER) {
		return -ENOTSUP;
	}

	*hdr = (struct zzb_zcl_hdr){
		.type = FIELD_GET(FCF_TYPE, p[0]),
		.flags = p[0] & FCF_FLAGS,
	};

	if (len < zzb_zcl_hdr_len(hdr)) {
		return -EINVAL;
	}

	if (hdr->flags & ZZB_ZCL_MANUFACTURER) {
		hdr->manufacturer = sys_get_le16(q);
		q += 2;
	}

	hdr->tsn = q[0];
	hdr->cmd = q[1];
	return zzb_zcl_hdr_len(hdr);
}

int zzb_zcl_type_len(uint8_t type)
{
	/* data8 to data64, then map8 to map64, uint8 to uint64 and int8 to int64 */
	if ((type >= 0x08 && type <= 0x0f) || (type >= 0x18 && type <= 0x2f)) {
		return (type & 0x07) + 1;
	}

	switch (type) {
	/* nodata, unk */
	case 0x00:
	case 0xff:
		return 0;
	/* bool, enum8 */
	case 0x10:
	case 0x30:
		return 1;
	/* enum16, semi, clusterId, attribId */
	case 0x31:
	case 0x38:
	case 0xe8:
	case 0xe9:
		return 2;
	/* single, ToD, date, UTC, bacOID */
	case 0x39:
	case 0xe0:
	case 0xe1:
	case 0xe2:
	case 0xea:
		return 4;
	/* double, EUI64 */
	case 0x3a:
	case 0xf0:
		return 8;
	/* key128 */
	case 0xf1:
		return 16;
	default:
		return -ENOTSUP;
	}
}

/* A length field of all ones marks an invalid string without characters (ZCL 2.6.2) */
static int value_len(uint8_t type, const uint8_t *p, size_t len)
{
	switch (type) {
	case ZZB_ZCL_OCTSTR:
	case ZZB_ZCL_STRING:
		if (len < 1) {
			return -EINVAL;
		}

		return p[0] == 0xff ? 1 : 1 + p[0];
	case TYPE_OCTSTR16:
	case TYPE_STRING16:
		if (len < 2) {
			return -EINVAL;
		}

		return sys_get_le16(p) == 0xffff ? 2 : 2 + sys_get_le16(p);
	default:
		return zzb_zcl_type_len(type);
	}
}

bool zzb_zcl_type_analog(uint8_t type)
{
	return (type >= 0x20 && type <= 0x2f) || (type >= 0x38 && type <= 0x3a) ||
	       (type >= 0xe0 && type <= 0xe2);
}

int zzb_zcl_value_len(uint8_t type, const uint8_t *p, size_t len)
{
	int n = value_len(type, p, len);

	if (n < 0) {
		return n;
	}

	return (size_t)n <= len ? n : -EINVAL;
}

uint32_t zzb_zcl_value_get(uint8_t type, const uint8_t *p)
{
	int len = MIN(zzb_zcl_type_len(type), 4);
	uint32_t value = 0;

	for (int i = len - 1; i >= 0; i--) {
		value = value << 8 | p[i];
	}

	return value;
}

void zzb_zcl_value_put(uint8_t type, uint32_t value, uint8_t *p)
{
	int len = MIN(zzb_zcl_type_len(type), 4);

	for (int i = 0; i < len; i++) {
		p[i] = value >> (8 * i);
	}
}
