/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fakes.h"
#include "zdo/zdo.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_req *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_set_tc_addr, struct zzb_node *, uint64_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_set_active_key, struct zzb_node *, uint8_t);
DEFINE_FAKE_VALUE_FUNC(uint16_t, zzb_nlme_get_short_addr, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(uint8_t, zzb_nlme_get_capability, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_bind_req, struct zzb_node *, const struct zzb_aps_binding *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_apsme_unbind_req, struct zzb_node *,
		       const struct zzb_aps_binding *);

struct frame_copy sent[8];
struct set_nwk_key_calls set_nwk_key;
struct zzb_aps_binding binding;

static int bind_copy(struct zzb_node *node, const struct zzb_aps_binding *arg)
{
	binding = *arg;
	return 0;
}

void zzb_nlme_set_nwk_key(struct zzb_node *node, const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t seq)
{
	set_nwk_key.count++;
	set_nwk_key.key = key;
	set_nwk_key.seq = seq;
}

/* Basic, Identify and On/Off servers of an On/Off Light */
static const uint16_t light_in[] = {0x0000, 0x0003, 0x0006};

/* On/Off client of an On/Off Light Switch */
static const uint16_t switch_out[] = {0x0006};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.device_version = 1,
		.in_clusters = light_in,
		.in_cluster_count = ARRAY_SIZE(light_in),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0103,
		.out_clusters = switch_out,
		.out_cluster_count = ARRAY_SIZE(switch_out),
	},
};

static const struct zzb_device device = {
	.manufacturer_code = 0x1234,
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
	};

	if (node == NULL) {
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return node;
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_apsde_data_req);
	RESET_FAKE(zzb_apsme_set_tc_addr);
	RESET_FAKE(zzb_nlme_set_active_key);
	RESET_FAKE(zzb_nlme_get_short_addr);
	RESET_FAKE(zzb_nlme_get_capability);
	RESET_FAKE(zzb_apsme_bind_req);
	RESET_FAKE(zzb_apsme_unbind_req);
	FFF_RESET_HISTORY();
	set_nwk_key = (struct set_nwk_key_calls){0};

	zzb_apsde_data_req_fake.custom_fake = apsde_data_req_take;
	zzb_apsme_bind_req_fake.custom_fake = bind_copy;
	zzb_apsme_unbind_req_fake.custom_fake = bind_copy;
	binding = (struct zzb_aps_binding){0};
	zzb_nlme_get_short_addr_fake.return_val = NODE_ADDR;
	/* Receiver on when idle, allocate address */
	zzb_nlme_get_capability_fake.return_val = 0x88;
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
	zzb_apsde_data_ind(test_node(), buf, ind);
}

void receive(uint16_t cluster, uint16_t dst_addr, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = dst_addr,
		.src_addr = REQUESTER_ADDR,
		.cluster = cluster,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};

	receive_ind(&ind, asdu, len);
}

void check_response(int i, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct frame_copy *copy = &sent[i];

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_SHORT);
	zassert_equal(copy->req.dst_addr, REQUESTER_ADDR);
	zassert_equal(copy->req.dst_ep, 0);
	zassert_equal(copy->req.src_ep, 0);
	zassert_equal(copy->req.profile, 0x0000);
	zassert_equal(copy->req.cluster, cluster);
	zassert_equal(copy->req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(copy->handle, ZZB_ZDO_HANDLE);
	zassert_equal(copy->len, len);
	zassert_mem_equal(copy->data, asdu, len);
}
