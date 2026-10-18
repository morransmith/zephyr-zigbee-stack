/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "radio/port.h"

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(zzb_radio_rx, struct zzb_radio_port *, struct net_buf *);

static const struct ieee802154_radio_api radio_api;

DEVICE_DEFINE(test_radio, "test_radio", NULL, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &radio_api);

static struct zzb_radio_port port;

/* Data frame: MAC header, one payload byte, then the two bytes the driver appends */
static const uint8_t rx_frame[] = {0x61, 0x88, 0xa3, 0xf5, 0x3e, 0x34,
				   0x52, 0x63, 0xf6, 0x42, 0xaa, 0xbb};

static uint8_t rx_data[ZZB_BUF_SIZE];
static size_t rx_len;
static struct zzb_frame_meta rx_meta;

static void rx_fake(struct zzb_radio_port *p, struct net_buf *buf)
{
	memcpy(rx_data, buf->data, buf->len);
	rx_len = buf->len;
	rx_meta = *zzb_frame_meta(buf);
	net_buf_unref(buf);
}

static struct net_pkt *rx_pkt(void)
{
	struct net_pkt *pkt =
		net_pkt_rx_alloc_with_buffer(NULL, sizeof(rx_frame), NET_AF_UNSPEC, 0, K_NO_WAIT);

	zassert_not_null(pkt);
	zassert_ok(net_pkt_write(pkt, rx_frame, sizeof(rx_frame)));
	return pkt;
}

static void flush_rx(void)
{
	struct k_work_sync sync;

	k_work_flush(&port.rx_work, &sync);
}

static void before(void *fixture)
{
	RESET_FAKE(zzb_radio_rx);
	zzb_radio_rx_fake.custom_fake = rx_fake;
	zassert_ok(zzb_radio_init(&port, DEVICE_GET(test_radio)));
}

ZTEST(zzb_radio_raw, test_rx_copies_frame_and_releases_pkt)
{
	struct k_mem_slab *rx_slab;
	struct k_mem_slab *tx_slab;
	struct net_buf_pool *rx_pool;
	struct net_buf_pool *tx_pool;
	struct net_pkt *pkt;
	uint32_t free_pkts;

	net_pkt_get_info(&rx_slab, &tx_slab, &rx_pool, &tx_pool);
	free_pkts = k_mem_slab_num_free_get(rx_slab);
	pkt = rx_pkt();
	net_pkt_set_ieee802154_lqi(pkt, 200);
	net_pkt_set_ieee802154_rssi_dbm(pkt, -60);

	zassert_ok(net_recv_data(NULL, pkt));
	zassert_equal(k_mem_slab_num_free_get(rx_slab), free_pkts);
	flush_rx();

	zassert_equal(zzb_radio_rx_fake.call_count, 1);
	zassert_equal_ptr(zzb_radio_rx_fake.arg0_val, &port);
	zassert_equal(rx_len, sizeof(rx_frame) - 2);
	zassert_mem_equal(rx_data, rx_frame, rx_len);
	zassert_equal(rx_meta.lqi, 200);
	zassert_equal(rx_meta.rssi, -60);
}

ZTEST(zzb_radio_raw, test_rx_without_buffer_leaves_pkt_with_driver)
{
	struct net_buf *bufs[CONFIG_ZZB_BUF_COUNT];
	struct net_pkt *pkt = rx_pkt();

	ARRAY_FOR_EACH(bufs, i) {
		bufs[i] = zzb_buf_alloc(K_NO_WAIT);
	}

	zassert_equal(net_recv_data(NULL, pkt), -ENOBUFS);
	zassert_equal(atomic_get(&pkt->atomic_ref), 1);
	net_pkt_unref(pkt);
	ARRAY_FOR_EACH(bufs, i) {
		net_buf_unref(bufs[i]);
	}
	flush_rx();

	zassert_equal(zzb_radio_rx_fake.call_count, 0);
}

ZTEST_SUITE(zzb_radio_raw, NULL, NULL, before, NULL, NULL);
