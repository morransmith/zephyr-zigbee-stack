/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "common/node.h"
#include "common/queue.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* Room for zcl/254/ffff/ffff */
#define KEY_LEN 20

/* Version and the longest value of a non-volatile attribute, an EUI64 */
#define RECORD_MAX_LEN 9

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

/* The value of a field of size octets */
static uint64_t field_get(const void *f, size_t size)
{
	switch (size) {
	case 8:
		return *(const uint64_t *)f;
	case 4:
		return *(const uint32_t *)f;
	case 2:
		return *(const uint16_t *)f;
	default:
		return *(const uint8_t *)f;
	}
}

uint64_t zzb_zcl_attr_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr)
{
	if (attr->flags & ZZB_ZCL_CONST) {
		return attr->value;
	}

	return field_get(field(cl, attr), ZZB_ZCL_STORAGE_SIZE(attr->type));
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
	default:
		n = zzb_zcl_type_len(attr->type);
		if (len < n) {
			return -ENOSPC;
		}

		zzb_zcl_value_put(attr->type, zzb_zcl_attr_value(cl, attr), p);
		return n;
	}
}

/* The check hook takes values of up to 32 bits */
static uint8_t check_value(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint64_t value)
{
	if ((attr->type == ZZB_ZCL_BOOL && value > 1) ||
	    (attr->type == ZZB_ZCL_UINT24 && value > BIT_MASK(24)) ||
	    (attr->type == ZZB_ZCL_INT24 && sign_extend(value, 23) != (int32_t)value) ||
	    (attr->type == ZZB_ZCL_UINT48 && value > BIT64_MASK(48)) ||
	    (attr->type == ZZB_ZCL_UINT56 && value > BIT64_MASK(56))) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	if (cl->def->check == NULL || ZZB_ZCL_STORAGE_SIZE(attr->type) > sizeof(uint32_t)) {
		return ZZB_ZCL_SUCCESS;
	}

	return cl->def->check(cl->values, attr, value);
}

uint8_t zzb_zcl_attr_check(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			   uint8_t type, const uint8_t *p, uint64_t *value)
{
	if (type != attr->type) {
		return ZZB_ZCL_INVALID_DATA_TYPE;
	}

	/* Strings are stored as pointers */
	if (!(attr->flags & ZZB_ZCL_WRITE) || (attr->flags & ZZB_ZCL_CONST) ||
	    ZZB_ZCL_IS_STRING(type)) {
		return ZZB_ZCL_READ_ONLY;
	}

	*value = zzb_zcl_value_get(type, p);
	return check_value(cl, attr, *value);
}

bool zzb_zcl_attr_write(const struct zzb_zcl_cluster *cl, const struct zzb_zcl_attr *attr,
			uint64_t value)
{
	uint8_t *f = (uint8_t *)cl->values + attr->offset;
	size_t size = ZZB_ZCL_STORAGE_SIZE(attr->type);
	bool changed = field_get(f, size) != value;

	switch (size) {
	case 8:
		*(uint64_t *)f = value;
		break;
	case 4:
		*(uint32_t *)f = value;
		break;
	case 2:
		*(uint16_t *)f = value;
		break;
	default:
		*f = value;
		break;
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

	err = zzb_settings_read(len, read_cb, cb_arg, rec, 1 + zzb_zcl_type_len(attr->type));
	if (err) {
		return err;
	}

	zzb_zcl_attr_write(cl, attr, zzb_zcl_value_get(attr->type, rec + 1));
	return 0;
}

/* Whether attribute id is in the scene extension field set of cl */
static bool in_scene(const struct zzb_zcl_cluster *cl, uint16_t id)
{
	for (int i = 0; i < cl->def->scene_attr_count; i++) {
		if (cl->def->scene_attrs[i].id == id) {
			return true;
		}
	}

	return false;
}

/* Calls the changed hook and the reporting check after a change of attr */
static void follow(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		   const struct zzb_zcl_attr *attr)
{
	if (cl->def->changed != NULL) {
		cl->def->changed(node, ep, cl->values, attr->id);
	}

	zzb_zcl_report_check(node, ep, cl, attr->id);
}

/*
 * Saves a non-volatile attribute and calls the changed hook. notify tells whether zcl_cb reports
 * the change, which the change of SceneValid follows.
 */
static void apply(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		  const struct zzb_zcl_attr *attr, bool notify)
{
	save(node, ep, cl, attr);
	follow(node, ep, cl, attr);
	if (in_scene(cl, attr->id)) {
		zzb_zcl_scenes_invalidate(node, ep, notify);
	}
}

void zzb_zcl_changed(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
		     uint16_t attr)
{
	apply(node, ep, cl, zzb_zcl_attr_find(cl, attr), true);

	if (node->zcl_cb != NULL) {
		node->zcl_cb(node, ep, cl->id, attr);
	}
}

/* The attribute id of the server cluster on endpoint ep, and the cluster in *cl, or NULL */
static const struct zzb_zcl_attr *server_attr(struct zzb_node *node, uint8_t ep, uint16_t cluster,
					      uint16_t id, const struct zzb_zcl_cluster **cl)
{
	const struct zzb_endpoint *e = zzb_node_endpoint(node, ep);

	*cl = e != NULL ? zzb_zcl_find_cluster(e, cluster, true) : NULL;
	return *cl != NULL ? zzb_zcl_attr_find(*cl, id) : NULL;
}

/* Writes value to attribute id of the server cluster on ep and reports a change */
static void store(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t id, uint32_t value,
		  bool notify)
{
	const struct zzb_zcl_cluster *cl;
	const struct zzb_zcl_attr *attr = server_attr(node, ep, cluster, id, &cl);

	if (attr == NULL || !zzb_zcl_attr_write(cl, attr, value)) {
		return;
	}

	apply(node, ep, cl, attr, notify);
	if (notify && node->zcl_cb != NULL) {
		node->zcl_cb(node, ep, cluster, id);
	}
}

void zzb_zcl_set(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr, uint32_t value)
{
	store(node, ep, cluster, attr, value, true);
}

void zzb_zcl_store(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr,
		   uint32_t value)
{
	store(node, ep, cluster, attr, value, false);
}

void zzb_zcl_update(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t id,
		    uint32_t value)
{
	const struct zzb_zcl_cluster *cl;
	const struct zzb_zcl_attr *attr = server_attr(node, ep, cluster, id, &cl);

	if (attr == NULL || !zzb_zcl_attr_write(cl, attr, value)) {
		return;
	}

	follow(node, ep, cl, attr);
	if (node->zcl_cb != NULL) {
		node->zcl_cb(node, ep, cluster, id);
	}
}

void zzb_zcl_save(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t id)
{
	const struct zzb_zcl_cluster *cl;
	const struct zzb_zcl_attr *attr = server_attr(node, ep, cluster, id, &cl);

	if (attr != NULL) {
		save(node, ep, cl, attr);
	}
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
	const struct zzb_zcl_cluster *cl;
	const struct zzb_zcl_attr *attr = server_attr(node, ep, cluster, id, &cl);
	uint64_t v;

	if (attr == NULL) {
		return -ENOENT;
	}

	if ((attr->flags & ZZB_ZCL_CONST) || ZZB_ZCL_IS_STRING(attr->type) ||
	    attr->type == ZZB_ZCL_EUI64) {
		return -EINVAL;
	}

	/* READ_ONLY refuses remote writes only */
	v = field_get(value, ZZB_ZCL_STORAGE_SIZE(attr->type));
	if (check_value(cl, attr, v) == ZZB_ZCL_INVALID_VALUE) {
		return -EINVAL;
	}

	if (zzb_zcl_attr_write(cl, attr, v)) {
		apply(node, ep, cl, attr, false);
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
