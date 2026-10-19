/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "zcl/zcl.h"

/* Direction of an attribute reporting configuration record (ZCL 2.5.7.1.2) */
#define DIRECTION_SENT     0x00
#define DIRECTION_RECEIVED 0x01

/* Direction, attribute ID, data type, minimum and maximum interval */
#define SENT_RECORD_LEN     8
/* Direction, attribute ID and timeout */
#define RECEIVED_RECORD_LEN 5

#define CONFIG_RECORDS_MAX (ZZB_ZCL_MAX_PAYLOAD / RECEIVED_RECORD_LEN)

/* An attribute reporting configuration record (ZCL 2.5.7.1) */
struct config_record {
	uint8_t direction;
	uint16_t id;
	uint8_t type;
	uint16_t min;
	uint16_t max;
	uint16_t change;
};

/*
 * Returns the length of the record at p, or -EINVAL. The reportable change of an analog type has
 * the length of the type in the record.
 */
static int parse_record(const uint8_t *p, size_t len, struct config_record *r)
{
	int change_len = 0;

	if (len < RECEIVED_RECORD_LEN) {
		return -EINVAL;
	}

	r->direction = p[0];
	r->id = sys_get_le16(p + 1);
	if (r->direction == DIRECTION_RECEIVED) {
		return RECEIVED_RECORD_LEN;
	}

	if (r->direction != DIRECTION_SENT || len < SENT_RECORD_LEN) {
		return -EINVAL;
	}

	r->type = p[3];
	r->min = sys_get_le16(p + 4);
	r->max = sys_get_le16(p + 6);
	r->change = 0;
	if (zzb_zcl_type_analog(r->type)) {
		change_len = zzb_zcl_value_len(r->type, p + SENT_RECORD_LEN, len - SENT_RECORD_LEN);
		if (change_len < 0) {
			return -EINVAL;
		}

		/* A reportable attribute of the stack has at most 16 bits */
		r->change =
			change_len == 1 ? p[SENT_RECORD_LEN] : sys_get_le16(p + SENT_RECORD_LEN);
	}

	return SENT_RECORD_LEN + change_len;
}

/* ZCL 2.5.7.3 */
static uint8_t check_record(const struct zzb_zcl_cluster *cl, const struct config_record *r)
{
	const struct zzb_zcl_attr *attr;

	/* No cluster of the stack receives reports */
	if (r->direction == DIRECTION_RECEIVED) {
		return ZZB_ZCL_UNREPORTABLE_ATTRIBUTE;
	}

	attr = zzb_zcl_attr_find(cl, r->id);
	if (attr == NULL) {
		return ZZB_ZCL_UNSUPPORTED_ATTRIBUTE;
	}

	/* Only integer values are reported */
	if (!(attr->flags & ZZB_ZCL_REPORT) || ZZB_ZCL_IS_STRING(attr->type)) {
		return ZZB_ZCL_UNREPORTABLE_ATTRIBUTE;
	}

	if (r->type != attr->type) {
		return ZZB_ZCL_INVALID_DATA_TYPE;
	}

	if (r->max != 0 && r->max < r->min) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

static struct zzb_zcl_report *find(struct zzb_node *node, uint8_t ep, uint16_t cluster,
				   uint16_t attr)
{
	ARRAY_FOR_EACH_PTR(node->zcl.reports, r) {
		if (r->in_use && r->ep == ep && r->cluster == cluster && r->attr == attr) {
			return r;
		}
	}

	return NULL;
}

static struct zzb_zcl_report *free_entry(struct zzb_node *node)
{
	ARRAY_FOR_EACH_PTR(node->zcl.reports, r) {
		if (!r->in_use) {
			return r;
		}
	}

	return NULL;
}

/* The checks have passed */
static uint8_t configure(struct zzb_node *node, const struct zzb_zcl_req *req,
			 const struct config_record *r)
{
	struct zzb_zcl_report *entry = find(node, req->cmd.ep, req->cmd.cluster, r->id);

	/* ZCL 2.5.7.1.6; the stack has no default configuration to revert to */
	if (r->max == 0xffff || (r->max == 0 && r->min == 0xffff)) {
		if (entry != NULL) {
			entry->in_use = false;
		}

		return ZZB_ZCL_SUCCESS;
	}

	if (entry == NULL) {
		entry = free_entry(node);
	}

	/* ZCL defines no status for a full table */
	if (entry == NULL) {
		return ZZB_ZCL_INSUFFICIENT_SPACE;
	}

	*entry = (struct zzb_zcl_report){
		.in_use = true,
		.ep = req->cmd.ep,
		.cluster = req->cmd.cluster,
		.attr = r->id,
		.min = r->min,
		.max = r->max,
		.change = r->change,
		.last = zzb_zcl_attr_value(req->cluster, zzb_zcl_attr_find(req->cluster, r->id)),
		.sent = k_uptime_get(),
	};
	return ZZB_ZCL_SUCCESS;
}

int zzb_zcl_configure_reporting(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	struct config_record records[CONFIG_RECORDS_MAX];
	uint8_t rsp[4 * CONFIG_RECORDS_MAX] = {ZZB_ZCL_SUCCESS};
	const uint8_t *p = req->cmd.payload;
	size_t left = req->cmd.len;
	int count = 0;
	size_t n = 0;

	/* Nothing is configured when a record cannot be parsed */
	while (left > 0) {
		int len = count < CONFIG_RECORDS_MAX ? parse_record(p, left, &records[count])
						     : -EINVAL;

		if (len < 0) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		count++;
		p += len;
		left -= len;
	}

	for (int i = 0; i < count; i++) {
		const struct config_record *r = &records[i];
		uint8_t status = check_record(req->cluster, r);

		if (status == ZZB_ZCL_SUCCESS) {
			status = configure(node, req, r);
		}

		if (status != ZZB_ZCL_SUCCESS) {
			rsp[n] = status;
			rsp[n + 1] = r->direction;
			sys_put_le16(r->id, rsp + n + 2);
			n += 4;
		}
	}

	/* ZCL 2.5.8.1: one SUCCESS record without direction and ID when every record succeeded */
	zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_CONFIGURE_REPORTING_RSP, rsp,
			MAX(n, 1));
	return ZZB_ZCL_RESPONDED;
}
