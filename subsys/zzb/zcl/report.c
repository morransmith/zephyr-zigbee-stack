/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "common/node.h"
#include "common/queue.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* Direction of an attribute reporting configuration record (ZCL 2.5.7.1.2) */
#define DIRECTION_SENT     0x00
#define DIRECTION_RECEIVED 0x01

/* Direction, attribute ID, data type, minimum and maximum interval */
#define SENT_RECORD_LEN     8
/* Direction, attribute ID and timeout */
#define RECEIVED_RECORD_LEN 5
/* A record of Read Reporting Configuration: direction and attribute ID */
#define READ_RECORD_LEN     3
/* Status, direction, attribute ID, data type, intervals and a reportable change of 16 bits */
#define READ_RSP_RECORD_MAX 11

#define CONFIG_RECORDS_MAX (ZZB_ZCL_MAX_PAYLOAD / RECEIVED_RECORD_LEN)

/* A report that APSDE-DATA refuses off a network or during a rejoin is sent again after this */
#define RETRY_MS 5000

#define RECORD_VERSION 1
#define RECORD_LEN     12
/* Room for zcl/rep/<i> with any int i */
#define NAME_LEN       20

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

/* Whether attr can have a configuration; no cluster of the stack receives reports */
static uint8_t reportable(const struct zzb_zcl_attr *attr, uint8_t direction)
{
	if (direction == DIRECTION_RECEIVED) {
		return ZZB_ZCL_UNREPORTABLE_ATTRIBUTE;
	}

	if (attr == NULL) {
		return ZZB_ZCL_UNSUPPORTED_ATTRIBUTE;
	}

	/* Only integer values are reported */
	if (!(attr->flags & ZZB_ZCL_REPORT) || ZZB_ZCL_IS_STRING(attr->type)) {
		return ZZB_ZCL_UNREPORTABLE_ATTRIBUTE;
	}

	return ZZB_ZCL_SUCCESS;
}

/* ZCL 2.5.7.3 */
static uint8_t check_record(const struct zzb_zcl_cluster *cl, const struct config_record *r)
{
	const struct zzb_zcl_attr *attr = zzb_zcl_attr_find(cl, r->id);
	uint8_t status = reportable(attr, r->direction);

	if (status != ZZB_ZCL_SUCCESS) {
		return status;
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

/* The time at which a report of r is due, or INT64_MAX */
static int64_t due_at(const struct zzb_zcl_report *r)
{
	int64_t t = INT64_MAX;

	if (r->retry != 0) {
		return r->retry;
	}

	if (r->pending) {
		t = r->sent + r->min * MSEC_PER_SEC;
	}

	if (r->max != 0) {
		t = MIN(t, r->sent + r->max * MSEC_PER_SEC);
	}

	return t;
}

static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	ARRAY_FOR_EACH_PTR(node->zcl.reports, r) {
		if (r->in_use) {
			next = MIN(next, due_at(r));
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.report_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.report_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

static void record_name(char name[NAME_LEN], int index)
{
	snprintk(name, NAME_LEN, "zcl/rep/%d", index);
}

/* A failed save is ignored; the next configuration writes the record again */
static void save(struct zzb_node *node, const struct zzb_zcl_report *r)
{
	uint8_t rec[RECORD_LEN] = {RECORD_VERSION, r->ep};
	char name[NAME_LEN];

	sys_put_le16(r->cluster, rec + 2);
	sys_put_le16(r->attr, rec + 4);
	sys_put_le16(r->min, rec + 6);
	sys_put_le16(r->max, rec + 8);
	sys_put_le16(r->change, rec + 10);
	record_name(name, r - node->zcl.reports);
	zzb_settings_save(node, name, rec, sizeof(rec));
}

static void remove_entry(struct zzb_node *node, struct zzb_zcl_report *r)
{
	char name[NAME_LEN];

	r->in_use = false;
	record_name(name, r - node->zcl.reports);
	zzb_settings_delete(node, name);
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
			remove_entry(node, entry);
			schedule(node);
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
	save(node, entry);
	schedule(node);
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

/* Writes the response record of the attribute id to p and returns its length (ZCL 2.5.10.1) */
static size_t read_record(struct zzb_node *node, const struct zzb_zcl_req *req, uint8_t direction,
			  uint16_t id, uint8_t *p)
{
	const struct zzb_zcl_attr *attr = zzb_zcl_attr_find(req->cluster, id);
	const struct zzb_zcl_report *entry = find(node, req->cmd.ep, req->cmd.cluster, id);
	size_t len = 9;

	p[0] = reportable(attr, direction);
	p[1] = direction;
	sys_put_le16(id, p + 2);
	if (p[0] == ZZB_ZCL_SUCCESS && entry == NULL) {
		p[0] = ZZB_ZCL_NOT_FOUND;
	}

	if (p[0] != ZZB_ZCL_SUCCESS) {
		return 4;
	}

	p[4] = attr->type;
	sys_put_le16(entry->min, p + 5);
	sys_put_le16(entry->max, p + 7);
	if (zzb_zcl_type_analog(attr->type)) {
		sys_put_le16(entry->change, p + 9);
		len += ZZB_ZCL_STORAGE_SIZE(attr->type);
	}

	return len;
}

int zzb_zcl_read_reporting(struct zzb_node *node, const struct zzb_zcl_req *req)
{
	const uint8_t *p = req->cmd.payload;
	uint8_t rsp[ZZB_ZCL_MAX_PAYLOAD];
	size_t n = 0;

	for (size_t i = 0; i + READ_RECORD_LEN <= req->cmd.len; i += READ_RECORD_LEN) {
		uint8_t record[READ_RSP_RECORD_MAX];
		size_t len = read_record(node, req, p[i], sys_get_le16(p + i + 1), record);

		/* ZCL 2.5.10.1: only the records that fit */
		if (len > sizeof(rsp) - n) {
			break;
		}

		memcpy(rsp + n, record, len);
		n += len;
	}

	zzb_zcl_respond(node, req, ZZB_ZCL_FRAME_GLOBAL, ZZB_ZCL_READ_REPORTING_RSP, rsp, n);
	return ZZB_ZCL_RESPONDED;
}

void zzb_zcl_report_check(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
			  uint16_t attr)
{
	struct zzb_zcl_report *r = cl->server ? find(node, ep, cl->id, attr) : NULL;
	const struct zzb_zcl_attr *a;
	uint16_t value;

	if (r == NULL) {
		return;
	}

	/* ZCL 2.5.11.2.2, 2.5.11.2.3 */
	a = zzb_zcl_attr_find(cl, attr);
	value = zzb_zcl_attr_value(cl, a);
	if (value == r->last ||
	    (zzb_zcl_type_analog(a->type) && abs((int)value - r->last) < r->change)) {
		return;
	}

	r->pending = true;
	schedule(node);
}

/* Sends the due reports of the endpoint and cluster of first in one frame (ZCL 2.5.11.2.5) */
static void send_reports(struct zzb_node *node, const struct zzb_zcl_report *first, int64_t now)
{
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, first->ep);
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, first->cluster, true);
	const struct zzb_zcl_hdr hdr = {
		.type = ZZB_ZCL_FRAME_GLOBAL,
		.flags = ZZB_ZCL_TO_CLIENT | ZZB_ZCL_DISABLE_DEFAULT_RSP,
		.tsn = node->zcl.tsn++,
		.cmd = ZZB_ZCL_REPORT_ATTRIBUTES,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_BINDING,
		.profile = ep->profile,
		.cluster = first->cluster,
		.src_ep = first->ep,
		.tx_options = ZZB_APS_TX_ACK,
	};
	bool in_frame[CONFIG_ZZB_ZCL_REPORT_COUNT] = {0};
	uint8_t payload[ZZB_ZCL_MAX_PAYLOAD];
	size_t n = 0;
	int err;

	ARRAY_FOR_EACH(node->zcl.reports, i) {
		const struct zzb_zcl_report *r = &node->zcl.reports[i];
		const struct zzb_zcl_attr *attr = zzb_zcl_attr_find(cl, r->attr);
		int len;

		if (!r->in_use || r->ep != first->ep || r->cluster != first->cluster ||
		    due_at(r) > now) {
			continue;
		}

		/* Attribute ID, data type and value; the next frame takes what does not fit */
		len = sizeof(payload) - n > 3 ? zzb_zcl_attr_encode(cl, attr, payload + n + 3,
								    sizeof(payload) - n - 3)
					      : -ENOSPC;
		if (len < 0) {
			break;
		}

		sys_put_le16(r->attr, payload + n);
		payload[n + 2] = attr->type;
		n += 3 + len;
		in_frame[i] = true;
	}

	err = zzb_zcl_send(node, &aps, &hdr, payload, n);

	ARRAY_FOR_EACH(node->zcl.reports, i) {
		struct zzb_zcl_report *r = &node->zcl.reports[i];

		if (!in_frame[i]) {
			continue;
		}

		/* Other errors, such as no binding, count as sent */
		if (err == -ENOTCONN || err == -EBUSY) {
			r->retry = now + RETRY_MS;
			continue;
		}

		r->last = zzb_zcl_attr_value(cl, zzb_zcl_attr_find(cl, r->attr));
		r->sent = now;
		r->pending = false;
		r->retry = 0;
	}
}

static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.report_timer);
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->zcl.reports, r) {
		if (r->in_use && due_at(r) <= now) {
			send_reports(node, r, now);
		}
	}

	schedule(node);
}

void zzb_zcl_report_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.report_timer, tick);
}

/* The attribute of r, when the device definition has it as a reportable attribute of a server */
static const struct zzb_zcl_attr *attr_of(struct zzb_node *node, const struct zzb_zcl_report *r,
					  const struct zzb_zcl_cluster **cl)
{
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, r->ep);
	const struct zzb_zcl_attr *attr;

	*cl = ep != NULL ? zzb_zcl_find_cluster(ep, r->cluster, true) : NULL;
	attr = *cl != NULL ? zzb_zcl_attr_find(*cl, r->attr) : NULL;
	return reportable(attr, DIRECTION_SENT) == ZZB_ZCL_SUCCESS ? attr : NULL;
}

int zzb_zcl_report_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
			void *cb_arg)
{
	struct zzb_zcl_report r;
	const struct zzb_zcl_cluster *cl;
	uint8_t rec[RECORD_LEN];
	int err;

	if (index < 0 || index >= ARRAY_SIZE(node->zcl.reports)) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	r = (struct zzb_zcl_report){
		.in_use = true,
		.ep = rec[1],
		.cluster = sys_get_le16(rec + 2),
		.attr = sys_get_le16(rec + 4),
		.min = sys_get_le16(rec + 6),
		.max = sys_get_le16(rec + 8),
		.change = sys_get_le16(rec + 10),
	};
	if (attr_of(node, &r, &cl) != NULL) {
		node->zcl.reports[index] = r;
	}

	return 0;
}

void zzb_zcl_report_start(struct zzb_node *node)
{
	const struct zzb_zcl_cluster *cl;
	int64_t now = k_uptime_get();

	ARRAY_FOR_EACH_PTR(node->zcl.reports, r) {
		if (r->in_use) {
			r->last = zzb_zcl_attr_value(cl, attr_of(node, r, &cl));
			r->sent = now;
		}
	}

	schedule(node);
}

void zzb_zcl_report_reset(struct zzb_node *node)
{
	char name[NAME_LEN];

	memset(node->zcl.reports, 0, sizeof(node->zcl.reports));
	k_work_cancel_delayable(&node->zcl.report_timer);
	for (int i = 0; i < ARRAY_SIZE(node->zcl.reports); i++) {
		record_name(name, i);
		zzb_settings_delete(node, name);
	}
}
