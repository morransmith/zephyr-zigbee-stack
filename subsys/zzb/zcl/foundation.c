/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>

#include "zcl/zcl.h"

/* Read attribute status record: attribute ID, status, then data type and value with SUCCESS */
#define STATUS_RECORD_LEN 3
#define VALUE_OFFSET      4

/* ZCL 2.5.1.3; an odd last octet is ignored (ZCL 2.3.2) */
static int read_attributes(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	uint8_t rsp[ZZB_ZCL_MAX_PAYLOAD];
	size_t n = 0;

	for (size_t i = 0; i + 2 <= req->cmd.len; i += 2) {
		uint16_t id = sys_get_le16(req->cmd.payload + i);
		const struct zzb_zcl_attr *attr = zzb_zcl_attr_find(req->cluster, id);
		size_t room = sizeof(rsp) - n;
		int len = -ENOSPC;

		if (room < STATUS_RECORD_LEN) {
			break;
		}

		sys_put_le16(id, rsp + n);
		if (attr == NULL) {
			rsp[n + 2] = ZZB_ZCL_UNSUPPORTED_ATTRIBUTE;
			n += STATUS_RECORD_LEN;
			continue;
		}

		if (room > VALUE_OFFSET) {
			len = zzb_zcl_attr_encode(req->cluster, attr, rsp + n + VALUE_OFFSET,
						  room - VALUE_OFFSET);
		}

		if (len < 0) {
			rsp[n + 2] = ZZB_ZCL_INSUFFICIENT_SPACE;
			n += STATUS_RECORD_LEN;
			continue;
		}

		rsp[n + 2] = ZZB_ZCL_SUCCESS;
		rsp[n + 3] = attr->type;
		n += VALUE_OFFSET + len;
	}

	zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_READ_ATTRIBUTES_RSP, rsp, n);
	return ZZB_ZCL_RESPONDED;
}

int zzb_zcl_foundation(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	switch (req->hdr.cmd) {
	case ZZB_ZCL_READ_ATTRIBUTES:
		return read_attributes(node, req);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}
