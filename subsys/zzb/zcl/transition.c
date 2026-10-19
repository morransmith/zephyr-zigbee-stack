/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "zcl/zcl.h"

/* Milliseconds of the unit of ZCL transition times */
#define TENTH_MS 100

void zzb_zcl_transition_start(struct zzb_zcl_transition *t, const struct zzb_zcl_cluster *cl,
			      const uint16_t *attrs, const uint16_t *to, size_t count,
			      uint32_t time_ds)
{
	__ASSERT_NO_MSG(count <= ARRAY_SIZE(t->attr) && time_ds > 0);

	t->start = k_uptime_get();
	t->duration = time_ds * TENTH_MS;
	t->count = count;
	for (size_t i = 0; i < count; i++) {
		t->attr[i] = attrs[i];
		t->from[i] = zzb_zcl_attr_value(cl, zzb_zcl_attr_find(cl, attrs[i]));
		t->to[i] = to[i];
		t->last[i] = t->from[i];
	}
}

bool zzb_zcl_transition_step(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_cluster *cl,
			     struct zzb_zcl_transition *t)
{
	int64_t elapsed = MIN(k_uptime_get() - t->start, t->duration);

	for (int i = 0; i < t->count; i++) {
		if (zzb_zcl_attr_value(cl, zzb_zcl_attr_find(cl, t->attr[i])) != t->last[i]) {
			t->count = 0;
			return false;
		}
	}

	for (int i = 0; i < t->count; i++) {
		t->last[i] = t->from[i] + (t->to[i] - t->from[i]) * elapsed / t->duration;
		zzb_zcl_update(node, ep, cl->id, t->attr[i], t->last[i]);
	}

	if (elapsed < t->duration) {
		return false;
	}

	zzb_zcl_transition_stop(node, ep, cl->id, t);
	return true;
}

void zzb_zcl_transition_stop(struct zzb_node *node, uint8_t ep, uint16_t cluster,
			     struct zzb_zcl_transition *t)
{
	for (int i = 0; i < t->count; i++) {
		zzb_zcl_save(node, ep, cluster, t->attr[i]);
	}

	t->count = 0;
}

uint16_t zzb_zcl_transition_remaining(const struct zzb_zcl_transition *t)
{
	int64_t left = t->start + t->duration - k_uptime_get();

	return t->count > 0 && left > 0 ? MIN(DIV_ROUND_UP(left, TENTH_MS), UINT16_MAX) : 0;
}
