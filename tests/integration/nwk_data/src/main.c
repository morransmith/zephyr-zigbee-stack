/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/buf.h"
#include "common/node.h"
#include "fake_radio.h"
#include "nwk/nwk.h"

#define PAN_ID      0x1a62
#define EXT_PAN_ID  0x00124b0000001a62ULL
#define PARENT_ADDR 0x0000
#define CHILD_ADDR  0x1234

/* The test network key, also in ci/check-frame-log.sh */
static const uint8_t nwk_key[ZZB_SEC_KEY_LEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

static struct zzb_node *parent;
static struct zzb_node *child;

/* NLDE callbacks, which APS defines from milestone 9 */
static struct {
	struct zzb_node *node;
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	uint16_t dst_addr;
	uint16_t src_addr;
	bool security_use;
	int count;
} ind;

static struct {
	struct zzb_node *node;
	uint8_t handle;
	enum zzb_mac_status status;
	int count;
} conf;

void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use)
{
	ind.node = node;
	memcpy(ind.data, buf->data, buf->len);
	ind.len = buf->len;
	ind.dst_addr = dst_addr;
	ind.src_addr = src_addr;
	ind.security_use = security_use;
	ind.count++;
	net_buf_unref(buf);
}

void zzb_nlde_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
	conf.node = node;
	conf.handle = handle;
	conf.status = status;
	conf.count++;
}

/* The test thread is cooperative and has a lower priority than the stack thread */
static void run_stack(void)
{
	k_yield();
}

static void join(struct zzb_node *node, uint16_t addr, struct zzb_node *peer, uint16_t peer_addr,
		 enum zzb_nwk_relationship relationship)
{
	zzb_nlme_set_pan_id(node, PAN_ID);
	zzb_nlme_set_short_addr(node, addr);
	zzb_nlme_set_ext_pan_id(node, EXT_PAN_ID);
	zzb_nlme_set_nwk_key(node, nwk_key, 0);
	zassert_not_null(zzb_nwk_neighbor_add(node, peer->eui64, peer_addr, relationship));
}

static void send(struct zzb_node *node, uint16_t dst_addr, const uint8_t *nsdu, size_t len,
		 uint8_t handle)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);
	net_buf_add_mem(buf, nsdu, len);
	zzb_frame_meta(buf)->handle = handle;
	zassert_ok(zzb_nlde_data_req(node, buf, dst_addr, 0, true));
	run_stack();

	zassert_equal(conf.count, 1);
	zassert_equal_ptr(conf.node, node);
	zassert_equal(conf.handle, handle);
	zassert_equal(conf.status, ZZB_MAC_STATUS_SUCCESS);
}

static void check_ind(struct zzb_node *node, uint16_t dst_addr, uint16_t src_addr,
		      const uint8_t *nsdu, size_t len)
{
	zassert_equal(ind.count, 1);
	zassert_equal_ptr(ind.node, node);
	zassert_equal(ind.dst_addr, dst_addr);
	zassert_equal(ind.src_addr, src_addr);
	zassert_true(ind.security_use);
	zassert_equal(ind.len, len);
	zassert_mem_equal(ind.data, nsdu, len);
}

static void *setup(void)
{
	struct zzb_node_config cfg = {
		.radio = fake_radios[0],
	};

	fake_radio_reset();
	zassert_ok(zzb_node_init(&cfg, &parent));
	cfg.radio = fake_radios[1];
	zassert_ok(zzb_node_init(&cfg, &child));

	join(parent, PARENT_ADDR, child, CHILD_ADDR, ZZB_NWK_REL_CHILD);
	join(child, CHILD_ADDR, parent, PARENT_ADDR, ZZB_NWK_REL_PARENT);
	return NULL;
}

static void before(void *fixture)
{
	memset(&ind, 0, sizeof(ind));
	memset(&conf, 0, sizeof(conf));
}

/* APS frames that tshark decodes: On/Off Toggle and its Default Response, Device_annce */
static const uint8_t toggle[] = {
	0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0x10, 0x01, 0x2a, 0x02,
};
static const uint8_t default_rsp[] = {
	0x00, 0x01, 0x06, 0x00, 0x04, 0x01, 0x01, 0x11, 0x18, 0x2a, 0x0b, 0x02, 0x00,
};
static const uint8_t device_annce[] = {
	0x08, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x12, 0x81, 0x34,
	0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
};

ZTEST(zzb_nwk_data, test_child_to_parent)
{
	send(child, PARENT_ADDR, toggle, sizeof(toggle), 1);
	check_ind(parent, PARENT_ADDR, CHILD_ADDR, toggle, sizeof(toggle));
}

ZTEST(zzb_nwk_data, test_parent_to_child)
{
	send(parent, CHILD_ADDR, default_rsp, sizeof(default_rsp), 2);
	check_ind(child, CHILD_ADDR, PARENT_ADDR, default_rsp, sizeof(default_rsp));
}

ZTEST(zzb_nwk_data, test_broadcast_from_child)
{
	send(child, 0xfffd, device_annce, sizeof(device_annce), 3);
	check_ind(parent, 0xfffd, CHILD_ADDR, device_annce, sizeof(device_annce));
}

ZTEST_SUITE(zzb_nwk_data, NULL, setup, before, NULL, NULL);
