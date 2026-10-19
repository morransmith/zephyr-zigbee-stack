/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "common/node.h"
#include "common/queue.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* Room for zcl/254/ffff/ffff */
#define KEY_LEN 20

/* Version and the longest value of a non-volatile attribute */
#define RECORD_MAX_LEN 3

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

uint16_t zzb_zcl_attr_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr)
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

		sys_put_le16(zzb_zcl_attr_value(cl, attr), p);
		return 2;
	default:
		if (len < 1) {
			return -ENOSPC;
		}

		p[0] = zzb_zcl_attr_value(cl, attr);
		return 1;
	}
}

static uint8_t check_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint16_t value)
{
	if (attr->type == ZZB_ZCL_BOOL && value > 1) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return cl->def->check != NULL ? cl->def->check(attr, value) : ZZB_ZCL_SUCCESS;
}

uint8_t zzb_zcl_attr_check(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint8_t type, const uint8_t *p, uint16_t *value)
{
	if (type != attr->type) {
		return ZZB_ZCL_INVALID_DATA_TYPE;
	}

	/* Strings are stored as pointers */
	if (!(attr->flags & ZZB_ZCL_WRITE) || (attr->flags & ZZB_ZCL_CONST) ||
	    ZZB_ZCL_IS_STRING(type)) {
		return ZZB_ZCL_READ_ONLY;
	}

	*value = type == ZZB_ZCL_UINT16 ? sys_get_le16(p) : p[0];
	return check_value(cl, attr, *value);
}

bool zzb_zcl_attr_write(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint16_t value)
{
	uint8_t *f = (uint8_t *)cl->values + attr->offset;
	bool changed = zzb_zcl_attr_value(cl, attr) != value;

	if (attr->type == ZZB_ZCL_UINT16) {
		*(uint16_t *)f = value;
	} else {
		*f = value;
	}

	return changed;
}

/* The key of the record of attr: zcl/<ep>/<cluster>/<attr> (section 15) */
static void key_of(char *key, uint8_t ep, const struct zzb_zcl_cluster *cl,
		   const struct zzb_zcl_attr *attr)
{
	snprintk(key, KEY_LEN, "zcl/%u/%04x/%04x", ep, cl->id, attr->id);
}

/* A failed save is ignored; the next change writes the record again */
static void save(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		 const struct zzb_zcl_attr *attr)
{
	uint8_t rec[RECORD_MAX_LEN] = {1};
	char key[KEY_LEN];
	int len = zzb_zcl_attr_encode(cl, attr, rec + 1, sizeof(rec) - 1);

	if (!(attr->flags & ZZB_ZCL_NV) || len < 0) {
		return;
	}

	key_of(key, ep, cl, attr);
	zzb_settings_save(node, key, rec, 1 + len);
}

void zzb_zcl_attr_delete(struct zzb_node *node)
{
	char key[KEY_LEN];

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		for (int j = 0; j < ep->cluster_count; j++) {
			const struct zzb_zcl_cluster *cl = &ep->clusters[j];

			for (int k = 0; cl->def != NULL && k < cl->def->attr_count; k++) {
				if (cl->def->attrs[k].flags & ZZB_ZCL_NV) {
					key_of(key, ep->id, cl, &cl->def->attrs[k]);
					zzb_settings_delete(node, key);
				}
			}
		}
	}
}

/* The non-volatile attribute id of a cluster on endpoint ep, and its cluster in *cl */
static const struct zzb_zcl_attr *find_nv(const struct zzb_node *node, unsigned long ep,
					  unsigned long cluster, unsigned long id,
					  const struct zzb_zcl_cluster **cl)
{
	const struct zzb_endpoint *e = ep <= UINT8_MAX ? zzb_node_endpoint(node, ep) : NULL;

	for (int i = 0; e != NULL && i < e->cluster_count; i++) {
		const struct zzb_zcl_attr *attr = zzb_zcl_attr_find(&e->clusters[i], id);

		if (e->clusters[i].id == cluster && attr != NULL && (attr->flags & ZZB_ZCL_NV)) {
			*cl = &e->clusters[i];
			return attr;
		}
	}

	return NULL;
}

int zzb_zcl_attr_load(struct zzb_node *node, const char *key, size_t len, settings_read_cb read_cb,
		      void *cb_arg)
{
	const struct zzb_zcl_cluster *cl;
	const struct zzb_zcl_attr *attr;
	uint8_t rec[RECORD_MAX_LEN];
	unsigned long ep, cluster, id;
	char *end;
	int err;

	/* <ep>/<cluster>/<attr> */
	ep = strtoul(key, &end, 10);
	cluster = *end == '/' ? strtoul(end + 1, &end, 16) : ULONG_MAX;
	id = *end == '/' ? strtoul(end + 1, &end, 16) : ULONG_MAX;
	attr = find_nv(node, ep, cluster, id, &cl);
	if (attr == NULL) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, 1 + ZZB_ZCL_STORAGE_SIZE(attr->type));
	if (err) {
		return err;
	}

	zzb_zcl_attr_write(cl, attr, attr->type == ZZB_ZCL_UINT16 ? sys_get_le16(rec + 1) : rec[1]);
	return 0;
}

/* Saves a non-volatile attribute and calls the changed hook */
static void apply(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		  const struct zzb_zcl_attr *attr)
{
	save(node, ep, cl, attr);

	if (cl->def->changed != NULL) {
		cl->def->changed(node, ep, cl->values, attr->id);
	}

	zzb_zcl_report_check(node, ep, cl, attr->id);
}

void zzb_zcl_changed(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		     uint16_t attr)
{
	apply(node, ep, cl, zzb_zcl_attr_find(cl, attr));

	if (node->zcl_cb != NULL) {
		node->zcl_cb(node, ep, cl->id, attr);
	}
}

/* Writes value to attribute id of the server cluster on ep and reports a change */
static void store(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t id, uint16_t value,
		  bool notify)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);
	const struct zzb_zcl_cluster *cl =
		e != NULL ? zzb_zcl_find_cluster(e, cluster, true) : NULL;
	const struct zzb_zcl_attr *attr = cl != NULL ? zzb_zcl_attr_find(cl, id) : NULL;

	if (attr == NULL || !zzb_zcl_attr_write(cl, attr, value)) {
		return;
	}

	apply(node, ep, cl, attr);
	if (notify && node->zcl_cb != NULL) {
		node->zcl_cb(node, ep, cluster, id);
	}
}

void zzb_zcl_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr, uint16_t value)
{
	store(node, ep, cluster, attr, value, true);
}

void zzb_zcl_store(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		   uint16_t value)
{
	store(node, ep, cluster, attr, value, false);
}

struct set_req {
	struct k_work work;
	struct zzb_node *node;
	uint8_t ep;
	uint16_t cluster;
	uint16_t attr;
	const void *value;
	int err;
};

static int set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t id, const void *value)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);
	const struct zzb_zcl_cluster *cl =
		e != NULL ? zzb_zcl_find_cluster(e, cluster, true) : NULL;
	const struct zzb_zcl_attr *attr = cl != NULL ? zzb_zcl_attr_find(cl, id) : NULL;
	uint16_t v;

	if (attr == NULL) {
		return -ENOENT;
	}

	if ((attr->flags & ZZB_ZCL_CONST) || ZZB_ZCL_IS_STRING(attr->type)) {
		return -EINVAL;
	}

	v = attr->type == ZZB_ZCL_UINT16 ? *(const uint16_t *)value : *(const uint8_t *)value;
	if (check_value(cl, attr, v) != ZZB_ZCL_SUCCESS) {
		return -EINVAL;
	}

	if (zzb_zcl_attr_write(cl, attr, v)) {
		apply(node, ep, cl, attr);
	}

	return 0;
}

static void set_handler(struct k_work *work)
{
	struct set_req *req = CONTAINER_OF(work, struct set_req, work);

	req->err = set(req->node, req->ep, req->cluster, req->attr, req->value);
}

int zzb_zcl_attr_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		     const void *value)
{
	struct set_req req = {
		.node = node,
		.ep = ep,
		.cluster = cluster,
		.attr = attr,
		.value = value,
	};

	zzb_call_sync(&req.work, set_handler);
	return req.err;
}
