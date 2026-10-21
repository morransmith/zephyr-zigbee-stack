/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zcl/binary_input.h>

#include "zcl/zcl.h"

/* ClusterRevision (ZCL 3.14.5.1) */
#define REVISION 1

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_ACTIVE_TEXT, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_binary_input, active_text),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_DESCRIPTION, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_binary_input, description),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_INACTIVE_TEXT, ZZB_ZCL_STRING, 0,
		     struct zzb_zcl_binary_input, inactive_text),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE, ZZB_ZCL_BOOL, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_binary_input, out_of_service),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_POLARITY, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_binary_input,
		     polarity),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_PRESENT_VALUE, ZZB_ZCL_BOOL,
		     ZZB_ZCL_WRITE | ZZB_ZCL_REPORT, struct zzb_zcl_binary_input, present_value),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_RELIABILITY, ZZB_ZCL_ENUM8, 0,
		     struct zzb_zcl_binary_input, reliability),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS, ZZB_ZCL_MAP8, ZZB_ZCL_REPORT,
		     struct zzb_zcl_binary_input, status_flags),
	ZZB_ZCL_ATTR(ZZB_ZCL_BINARY_INPUT_APPLICATION_TYPE, ZZB_ZCL_UINT32, 0,
		     struct zzb_zcl_binary_input, application_type),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* ZCL 3.14.11.2: PresentValue is writable while OutOfService is TRUE */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	const struct zzb_zcl_binary_input *v = values;

	if (attr->id == ZZB_ZCL_BINARY_INPUT_PRESENT_VALUE && !v->out_of_service) {
		return ZZB_ZCL_READ_ONLY;
	}

	return ZZB_ZCL_SUCCESS;
}

/* StatusFlags with FAULT and OUT_OF_SERVICE derived from Reliability and OutOfService */
static uint8_t status_flags(const struct zzb_zcl_binary_input *v)
{
	uint8_t flags = v->status_flags & ~(ZZB_ZCL_BINARY_INPUT_FLAG_FAULT |
					    ZZB_ZCL_BINARY_INPUT_FLAG_OUT_OF_SERVICE);

	if (v->reliability != ZZB_ZCL_BINARY_INPUT_RELIABILITY_NO_FAULT_DETECTED) {
		flags |= ZZB_ZCL_BINARY_INPUT_FLAG_FAULT;
	}

	if (v->out_of_service) {
		flags |= ZZB_ZCL_BINARY_INPUT_FLAG_OUT_OF_SERVICE;
	}

	return flags;
}

/* Reliability and StatusFlags are set by the application, so their flags come without zcl_cb */
static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	switch (attr) {
	case ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE:
		zzb_zcl_set(node, ep, ZZB_ZCL_BINARY_INPUT, ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS,
			    status_flags(values));
		break;
	case ZZB_ZCL_BINARY_INPUT_RELIABILITY:
	case ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS:
		zzb_zcl_store(node, ep, ZZB_ZCL_BINARY_INPUT, ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS,
			      status_flags(values));
		break;
	default:
		break;
	}
}

static void init(void *values)
{
	struct zzb_zcl_binary_input *v = values;

	v->out_of_service = false;
}

/* OutOfService may come from its record */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_store(node, ep, ZZB_ZCL_BINARY_INPUT, ZZB_ZCL_BINARY_INPUT_STATUS_FLAGS,
		      status_flags(values));
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_BINARY_INPUT, ZZB_ZCL_BINARY_INPUT_OUT_OF_SERVICE, false);
}

const struct zzb_zcl_cluster_def zzb_zcl_binary_input_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.check = check,
	.changed = changed,
};
