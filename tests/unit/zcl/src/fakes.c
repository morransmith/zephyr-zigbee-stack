/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_req *);

struct test_values values1;
struct test_values values2;

static const uint8_t product_code[] = {0x03, 0x00, 'Z', 'Z'};

struct zzb_zcl_identify identify;
struct zzb_zcl_on_off on_off;

struct zzb_zcl_basic basic = {
	.app_version = 1,
	.stack_version = 2,
	.hw_version = 3,
	.manufacturer_name = "zzb",
	.model_id = "test",
	.date_code = "20261019",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
	.product_code = product_code,
	.product_url = "u",
	.sw_build_id = "1.0",
};
struct cmd_calls cmd_calls;
int cmd_status;
struct changed_calls hook_calls;
struct zcl_cb_calls zcl_cb_calls;
struct effect_calls effect_calls;
struct frame_copy sent[8];
struct membership memberships[CONFIG_ZZB_APS_GROUP_COUNT];

/* The membership of group and ep, or a free one with ep 0 */
static struct membership *find_membership(uint16_t group, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->group == group && m->ep == ep) {
			return m;
		}
	}

	return NULL;
}

bool zzb_aps_group_member(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	return ep != 0 && find_membership(group, ep) != NULL;
}

int zzb_apsme_add_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	struct membership *m = find_membership(group, ep);

	if (m == NULL) {
		m = find_membership(0, 0);
	}

	if (m == NULL) {
		return -ENOMEM;
	}

	*m = (struct membership){.group = group, .ep = ep};
	return 0;
}

int zzb_apsme_remove_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	struct membership *m = find_membership(group, ep);

	if (m == NULL || ep == 0) {
		return -ENOENT;
	}

	*m = (struct membership){0};
	return 0;
}

void zzb_apsme_remove_all_groups(struct zzb_node *node, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->ep == ep) {
			*m = (struct membership){0};
		}
	}
}

int zzb_aps_group_capacity(struct zzb_node *node)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(memberships, m) {
		count += m->ep == 0;
	}

	return count;
}

int zzb_aps_groups_of(struct zzb_node *node, uint8_t ep, uint16_t *groups, int max)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->ep == ep && count < max) {
			groups[count++] = m->group;
		}
	}

	return count;
}

static int test_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	cmd_calls.count++;
	cmd_calls.cmd = *cmd;
	zassert_true(cmd->len <= sizeof(cmd_calls.payload));
	memcpy(cmd_calls.payload, cmd->payload, cmd->len);
	cmd_calls.values = values;
	return cmd_status;
}

static uint8_t test_check(const struct zzb_zcl_attr *attr, uint16_t value)
{
	return attr->id == 0x0003 && value > TEST_E8_MAX ? ZZB_ZCL_INVALID_VALUE : ZZB_ZCL_SUCCESS;
}

static void test_changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	zassert_true(hook_calls.count < ARRAY_SIZE(hook_calls.attr));
	zassert_equal_ptr(values, ep == 1 ? &values1 : &values2);
	hook_calls.ep[hook_calls.count] = ep;
	hook_calls.attr[hook_calls.count++] = attr;
}

static void test_reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, TEST_CLUSTER, 0x0001, 0);
	zzb_zcl_set(node, ep, TEST_CLUSTER, 0x0002, 0);
}

static void record_zcl_cb(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	int i = zcl_cb_calls.count++;

	zassert_true(i < ARRAY_SIZE(zcl_cb_calls.attr));
	zcl_cb_calls.ep[i] = ep;
	zcl_cb_calls.cluster[i] = cluster;
	zcl_cb_calls.attr[i] = attr;
}

static void record_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
			  uint8_t variant)
{
	effect_calls = (struct effect_calls){
		.count = effect_calls.count + 1,
		.ep = ep,
		.cluster = cluster,
		.effect = effect,
		.variant = variant,
		.zcl_cb_count = zcl_cb_calls.count,
	};
}

static const struct zzb_zcl_attr test_attrs[] = {
	ZZB_ZCL_ATTR(0x0000, ZZB_ZCL_BOOL, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT, struct test_values, b),
	ZZB_ZCL_ATTR(0x0001, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT, struct test_values, u8),
	ZZB_ZCL_ATTR(0x0002, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct test_values, u16),
	ZZB_ZCL_ATTR(0x0003, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE, struct test_values, e8),
	ZZB_ZCL_ATTR(0x0004, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE, struct test_values, map8),
	ZZB_ZCL_ATTR(0x0005, ZZB_ZCL_STRING, 0, struct test_values, str),
	ZZB_ZCL_ATTR(0x0006, ZZB_ZCL_OCTSTR, 0, struct test_values, octstr),
	ZZB_ZCL_ATTR(0x0007, ZZB_ZCL_STRING, 0, struct test_values, null_str),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, TEST_REVISION),
};

static const struct zzb_zcl_cluster_def test_def = {
	.attrs = test_attrs,
	.attr_count = ARRAY_SIZE(test_attrs),
	.reset = test_reset,
	.cmd = test_cmd,
	.check = test_check,
	.changed = test_changed,
};

static const struct zzb_zcl_cluster ep1_clusters[] = {
	{.id = TEST_CLUSTER, .server = true, .values = &values1, .def = &test_def},
	{.id = ON_OFF},
};

static const struct zzb_zcl_cluster ep2_clusters[] = {
	{.id = TEST_CLUSTER, .server = true, .values = &values2, .def = &test_def},
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
};

static const struct zzb_zcl_cluster ep3_clusters[] = {
	{.id = ON_OFF},
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep1_clusters,
		.cluster_count = ARRAY_SIZE(ep1_clusters),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep2_clusters,
		.cluster_count = ARRAY_SIZE(ep2_clusters),
	},
	{
		.id = 3,
		.profile = 0x0104,
		.device_id = 0x0103,
		.clusters = ep3_clusters,
		.cluster_count = ARRAY_SIZE(ep3_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

static int apsde_data_req_take(struct zzb_node *node, struct net_buf *buf,
			       const struct zzb_apsde_req *req)
{
	struct frame_copy *copy = &sent[zzb_apsde_data_req_fake.call_count - 1];

	zassert_true(zzb_apsde_data_req_fake.call_count <= ARRAY_SIZE(sent));
	zassert_true(buf->len <= sizeof(copy->data));
	copy->req = *req;
	copy->handle = zzb_frame_meta(buf)->handle;
	memcpy(copy->data, buf->data, buf->len);
	copy->len = buf->len;
	net_buf_unref(buf);
	return 0;
}

struct zzb_node *test_node(void)
{
	static struct zzb_node *node;
	const struct zzb_node_config cfg = {
		.eui64 = NODE_EUI64,
		.device = &device,
		.zcl_cb = record_zcl_cb,
		.zcl_effect_cb = record_effect,
	};

	if (node == NULL) {
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return node;
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_apsde_data_req);
	FFF_RESET_HISTORY();

	zzb_apsde_data_req_fake.custom_fake = apsde_data_req_take;
	cmd_calls = (struct cmd_calls){0};
	cmd_status = ZZB_ZCL_SUCCESS;
	hook_calls = (struct changed_calls){0};
	zcl_cb_calls = (struct zcl_cb_calls){0};
	effect_calls = (struct effect_calls){0};
	memset(memberships, 0, sizeof(memberships));
}

void check_no_buffer_leak(void)
{
	struct net_buf *bufs[CONFIG_ZZB_BUF_COUNT];

	ARRAY_FOR_EACH(bufs, i) {
		bufs[i] = zzb_buf_alloc(K_NO_WAIT);
		zassert_not_null(bufs[i], "buffer leak");
	}

	ARRAY_FOR_EACH(bufs, i) {
		net_buf_unref(bufs[i]);
	}
}

void receive_ind(const struct zzb_apsde_ind *ind, const uint8_t *asdu, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(test_node(), buf, ind);
}

void receive_ep(uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ep,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = cluster,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};

	receive_ind(&ind, asdu, len);
}

void receive(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	receive_ep(1, cluster, asdu, len);
}

void check_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct frame_copy *copy = &sent[i];

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_SHORT);
	zassert_equal(copy->req.dst_addr, REQUESTER_ADDR);
	zassert_equal(copy->req.dst_ep, REQUESTER_EP);
	zassert_equal(copy->req.src_ep, ep);
	zassert_equal(copy->req.profile, 0x0104);
	zassert_equal(copy->req.cluster, cluster);
	zassert_equal(copy->req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(copy->handle, ZZB_ZCL_HANDLE);
	zassert_equal(copy->len, len);
	zassert_mem_equal(copy->data, asdu, len);
}

struct reboot_call {
	struct k_work work;
	int err;
};

static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_zcl_init(test_node());
	call->err = zzb_settings_load(test_node());
	if (call->err == 0) {
		zzb_zcl_start(test_node());
	}
}

int reboot(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void reset_handler(struct k_work *work)
{
	zzb_zcl_reset(test_node());
}

void reset_node(void)
{
	struct k_work work;

	zzb_call_sync(&work, reset_handler);
}
