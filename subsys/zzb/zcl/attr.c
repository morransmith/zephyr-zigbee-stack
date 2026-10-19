/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "zcl/zcl.h"

const struct zzb_zcl_attr *zzb_zcl_attr_find(const struct zzb_zcl_cluster *cl, uint16_t id)
{
	if (cl->def == NULL) {
		return NULL;
	}

	for (int i = 0; i < cl->def->attr_count; i++) {
		if (cl->def->attrs[i].id == id) {
			return &cl->def->attrs[i];
		}
	}

	return NULL;
}

static const void *field(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr)
{
	return (const uint8_t *)cl->values + attr->offset;
}

/* The value of an attribute of an integer type */
static uint16_t integer(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr)
{
	if (attr->flags & ZZB_ZCL_CONST) {
		return attr->value;
	}

	if (attr->type == ZZB_ZCL_UINT16) {
		return *(const uint16_t *)field(cl, attr);
	}

	return *(const uint8_t *)field(cl, attr);
}

int zzb_zcl_attr_encode(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint8_t *p, size_t len)
{
	const char *str;
	const uint8_t *octstr;
	size_t n;

	switch (attr->type) {
	case ZZB_ZCL_STRING:
		str = *(const char *const *)field(cl, attr);
		n = str != NULL ? strlen(str) : 0;
		if (len < 1 + n) {
			return -ENOSPC;
		}

		p[0] = n;
		memcpy(p + 1, str, n);
		return 1 + n;
	case ZZB_ZCL_OCTSTR:
		octstr = *(const uint8_t *const *)field(cl, attr);
		n = octstr != NULL ? octstr[0] : 0;
		if (len < 1 + n) {
			return -ENOSPC;
		}

		p[0] = n;
		memcpy(p + 1, octstr + 1, n);
		return 1 + n;
	case ZZB_ZCL_UINT16:
		if (len < 2) {
			return -ENOSPC;
		}

		sys_put_le16(integer(cl, attr), p);
		return 2;
	default:
		if (len < 1) {
			return -ENOSPC;
		}

		p[0] = integer(cl, attr);
		return 1;
	}
}
