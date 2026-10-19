/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "fakes.h"
#include "zcl/zcl.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_req *);

struct test_values values1;
struct test_values values2;
struct cmd_calls cmd_calls;
int cmd_status;
struct changed_calls hook_calls;
struct zcl_cb_calls zcl_cb_calls;
struct frame_copy sent[8];

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

static void record_zcl_cb(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	int i = zcl_cb_calls.count++;

	zassert_true(i < ARRAY_SIZE(zcl_cb_calls.attr));
	zcl_cb_calls.ep[i] = ep;
	zcl_cb_calls.cluster[i] = cluster;
	zcl_cb_calls.attr[i] = attr;
}

static const struct zzb_zcl_attr test_attrs[] = {
	ZZB_ZCL_ATTR(0x0000, ZZB_ZCL_BOOL, ZZB_ZCL_WRITE, struct test_values, b),
	ZZB_ZCL_ATTR(0x0001, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE, struct test_values, u8),
	ZZB_ZCL_ATTR(0x0002, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE, struct test_values, u16),
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

void receive(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = 1,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = cluster,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};

	receive_ind(&ind, asdu, len);
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
