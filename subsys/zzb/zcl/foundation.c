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

/* A record of Write Attributes after the checks */
struct write_record {
	uint16_t id;
	const struct zzb_zcl_attr *attr;
	uint8_t status;
	uint16_t value;
};

/* Every record has an attribute ID and a data type */
#define WRITE_RECORDS_MAX (ZZB_ZCL_MAX_PAYLOAD / 3)

/*
 * Parses the records of a Write Attributes command and runs the checks of ZCL 2.5.3.3. Returns the
 * record count, or -EINVAL for a record that cannot be parsed.
 */
static int check_writes(const struct zzb_zcl_req *req, struct write_record *records)
{
	const uint8_t *p = req->cmd.payload;
	size_t left = req->cmd.len;
	int count = 0;

	while (left > 0) {
		struct write_record *r = &records[count];
		int len = left >= 3 ? zzb_zcl_value_len(p[2], p + 3, left - 3) : -EINVAL;

		if (len < 0 || count == WRITE_RECORDS_MAX) {
			return -EINVAL;
		}

		r->id = sys_get_le16(p);
		r->attr = zzb_zcl_attr_find(req->cluster, r->id);
		r->status = r->attr != NULL ? zzb_zcl_attr_check(req->cluster, r->attr, p[2], p + 3,
								 &r->value)
					    : ZZB_ZCL_UNSUPPORTED_ATTRIBUTE;
		count++;
		p += 3 + len;
		left -= 3 + len;
	}

	return count;
}

/* ZCL 2.5.3.3, 2.5.4, 2.5.6.3 */
static int write_attributes(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	struct write_record records[WRITE_RECORDS_MAX];
	int count = check_writes(req, records);
	bool changed[WRITE_RECORDS_MAX];
	uint8_t rsp[1 + 3 * WRITE_RECORDS_MAX] = {ZZB_ZCL_SUCCESS};
	size_t n = 0;

	if (count < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	for (int i = 0; i < count; i++) {
		if (records[i].status != ZZB_ZCL_SUCCESS) {
			rsp[n] = records[i].status;
			sys_put_le16(records[i].id, rsp + n + 1);
			n += 3;
		}
	}

	/* An undivided write with a failed record writes nothing */
	for (int i = 0; i < count; i++) {
		changed[i] = records[i].status == ZZB_ZCL_SUCCESS &&
			     !(req->hdr.cmd == ZZB_ZCL_WRITE_UNDIVIDED && n > 0) &&
			     zzb_zcl_attr_write(req->cluster, records[i].attr, records[i].value);
	}

	/* ZCL 2.5.5.1.2: one SUCCESS record without an ID when every attribute is written */
	if (req->hdr.cmd != ZZB_ZCL_WRITE_NO_RSP) {
		zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_WRITE_ATTRIBUTES_RSP, rsp,
				MAX(n, 1));
	}

	for (int i = 0; i < count; i++) {
		if (changed[i]) {
			zzb_zcl_changed(node, req->cmd.ep, req->cluster, records[i].id);
		}
	}

	return ZZB_ZCL_RESPONDED;
}

/* Attribute information records of 3 octets after discovery complete */
#define DISCOVER_MAX ((ZZB_ZCL_MAX_PAYLOAD - 1) / 3)

/* ZCL 2.5.13.3 */
static int discover_attributes(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	const struct zzb_zcl_cluster_def *def = req->cluster->def;
	uint8_t rsp[1 + 3 * DISCOVER_MAX] = {1};
	size_t n = 1;
	uint16_t start;
	uint8_t max;

	if (req->cmd.len < 3) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	start = sys_get_le16(req->cmd.payload);
	max = MIN(req->cmd.payload[2], DISCOVER_MAX);

	for (int i = 0; def != NULL && i < def->attr_count; i++) {
		const struct zzb_zcl_attr *attr = &def->attrs[i];

		if (attr->id < start) {
			continue;
		}

		/* Discovery is not complete */
		if (n == 1 + 3 * max) {
			rsp[0] = 0;
			break;
		}

		sys_put_le16(attr->id, rsp + n);
		rsp[n + 2] = attr->type;
		n += 3;
	}

	zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_DISCOVER_ATTRIBUTES_RSP, rsp, n);
	return ZZB_ZCL_RESPONDED;
}

int zzb_zcl_foundation(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	switch (req->hdr.cmd) {
	case ZZB_ZCL_READ_ATTRIBUTES:
		return read_attributes(node, req);
	case ZZB_ZCL_WRITE_ATTRIBUTES:
	case ZZB_ZCL_WRITE_UNDIVIDED:
	case ZZB_ZCL_WRITE_NO_RSP:
		return write_attributes(node, req);
	case ZZB_ZCL_DISCOVER_ATTRIBUTES:
		return discover_attributes(node, req);
	case ZZB_ZCL_CONFIGURE_REPORTING:
		return zzb_zcl_configure_reporting(node, req);
	case ZZB_ZCL_READ_REPORTING:
		return zzb_zcl_read_reporting(node, req);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}
