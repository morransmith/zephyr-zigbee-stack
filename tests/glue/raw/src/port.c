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
#include "mac/mac.h"
#include "radio/port.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(enum ieee802154_hw_caps, radio_get_caps, const struct device *);
FAKE_VALUE_FUNC(int, radio_cca, const struct device *);
FAKE_VALUE_FUNC(int, radio_tx, const struct device *, enum ieee802154_tx_mode, struct net_pkt *,
		struct net_buf *);
FAKE_VOID_FUNC(zzb_radio_rx, struct zzb_radio_port *, struct net_buf *);
FAKE_VOID_FUNC(zzb_radio_tx_done, struct zzb_radio_port *, struct net_buf *, enum zzb_mac_status,
	       bool);

static const struct ieee802154_radio_api radio_api = {
	.get_capabilities = radio_get_caps,
	.cca = radio_cca,
	.tx = radio_tx,
};

DEVICE_DEFINE(test_radio, "test_radio", NULL, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &radio_api);

static struct zzb_radio_port port;

/* Data frame with ACK request from 0x1234 to 0x0000 in PAN 0x1a62, one payload byte */
static const uint8_t unicast_frame[] = {0x61, 0x88, 0x01, 0x62, 0x1a, 0x00, 0x00, 0x34, 0x12, 0x42};

/* The same frame to 0xffff without ACK request */
static const uint8_t bcast_frame[] = {0x41, 0x88, 0x01, 0x62, 0x1a, 0xff, 0xff, 0x34, 0x12, 0x42};

/* ACK for unicast_frame; ack[0] is 0x12 when frame pending is set */
static uint8_t ack[] = {0x02, 0x00, 0x01};

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

/* unicast_frame and the two bytes the driver appends with CONFIG_IEEE802154_L2_PKT_INCL_FCS */
static struct net_pkt *rx_pkt(void)
{
	static const uint8_t trailer[] = {0xaa, 0xbb};
	struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(
		NULL, sizeof(unicast_frame) + sizeof(trailer), NET_AF_UNSPEC, 0, K_NO_WAIT);

	zassert_not_null(pkt);
	zassert_ok(net_pkt_write(pkt, unicast_frame, sizeof(unicast_frame)));
	zassert_ok(net_pkt_write(pkt, trailer, sizeof(trailer)));
	return pkt;
}

static void flush_rx(void)
{
	struct k_work_sync sync;

	k_work_flush(&port.rx_work, &sync);
}

static void tx_done_fake(struct zzb_radio_port *p, struct net_buf *buf, enum zzb_mac_status status,
			 bool frame_pending)
{
	net_buf_unref(buf);
}

/* tx() of a driver that receives the ACK in ack[] */
static int tx_acked(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		    struct net_buf *frag)
{
	struct net_pkt *ack_pkt =
		net_pkt_rx_alloc_with_buffer(NULL, sizeof(ack), NET_AF_UNSPEC, 0, K_NO_WAIT);

	zassert_not_null(ack_pkt);
	zassert_ok(net_pkt_write(ack_pkt, ack, sizeof(ack)));
	zassert_equal(ieee802154_handle_ack(NULL, ack_pkt), NET_OK);
	net_pkt_unref(ack_pkt);
	return 0;
}

static struct net_buf *tx_buf(const uint8_t *frame, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, frame, len);
	return buf;
}

static void flush_tx(void)
{
	struct k_work_sync sync;

	k_work_flush(&port.tx_work, &sync);
}

static void transmit(struct net_buf *buf)
{
	RESET_FAKE(zzb_radio_tx_done);
	zzb_radio_tx_done_fake.custom_fake = tx_done_fake;

	zassert_ok(zzb_radio_tx(&port, buf));
	flush_tx();

	zassert_equal(zzb_radio_tx_done_fake.call_count, 1);
	zassert_equal_ptr(zzb_radio_tx_done_fake.arg0_val, &port);
	zassert_equal_ptr(zzb_radio_tx_done_fake.arg1_val, buf);
}

static void before(void *fixture)
{
	RESET_FAKE(radio_get_caps);
	RESET_FAKE(radio_cca);
	RESET_FAKE(radio_tx);
	RESET_FAKE(zzb_radio_rx);
	RESET_FAKE(zzb_radio_tx_done);
	radio_get_caps_fake.return_val = IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_CSMA;
	zzb_radio_rx_fake.custom_fake = rx_fake;
	zzb_radio_tx_done_fake.custom_fake = tx_done_fake;
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
	zassert_equal(rx_len, sizeof(unicast_frame));
	zassert_mem_equal(rx_data, unicast_frame, rx_len);
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

ZTEST(zzb_radio_raw, test_init_requires_ack_offload)
{
	struct zzb_radio_port other;

	radio_get_caps_fake.return_val = IEEE802154_HW_CSMA;

	zassert_equal(zzb_radio_init(&other, DEVICE_GET(test_radio)), -ENOTSUP);
}

ZTEST(zzb_radio_raw, test_tx_uses_hardware_csma_ca)
{
	struct net_buf *buf = tx_buf(bcast_frame, sizeof(bcast_frame));

	transmit(buf);

	zassert_equal(radio_tx_fake.call_count, 1);
	zassert_equal_ptr(radio_tx_fake.arg0_val, DEVICE_GET(test_radio));
	zassert_equal(radio_tx_fake.arg1_val, IEEE802154_TX_MODE_CSMA_CA);
	zassert_equal_ptr(radio_tx_fake.arg3_val, buf);
	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_false(zzb_radio_tx_done_fake.arg3_val);
}

ZTEST(zzb_radio_raw, test_tx_errors_map_to_mac_status)
{
	static const struct {
		int err;
		enum zzb_mac_status status;
	} cases[] = {
		{-EBUSY, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE},
		{-ENOMSG, ZZB_MAC_STATUS_NO_ACK},
		{-EIO, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE},
	};

	ARRAY_FOR_EACH_PTR(cases, c) {
		radio_tx_fake.return_val = c->err;
		transmit(tx_buf(bcast_frame, sizeof(bcast_frame)));
		zassert_equal(zzb_radio_tx_done_fake.arg2_val, c->status, "tx() returned %d",
			      c->err);
	}
}

ZTEST(zzb_radio_raw, test_tx_is_busy_until_confirm)
{
	struct net_buf *first = tx_buf(bcast_frame, sizeof(bcast_frame));
	struct net_buf *second = tx_buf(bcast_frame, sizeof(bcast_frame));

	zassert_ok(zzb_radio_tx(&port, first));
	zassert_equal(zzb_radio_tx(&port, second), -EBUSY);
	flush_tx();

	transmit(second);
}

ZTEST(zzb_radio_raw, test_ack_gives_frame_pending)
{
	radio_tx_fake.custom_fake = tx_acked;

	ack[0] = 0x02;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));
	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_false(zzb_radio_tx_done_fake.arg3_val);

	ack[0] = 0x12;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));
	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_true(zzb_radio_tx_done_fake.arg3_val);
}

/* esp32 returns 0 from tx() when the ACK is missing */
ZTEST(zzb_radio_raw, test_ack_request_without_ack_is_no_ack)
{
	ack[0] = 0x12;
	radio_tx_fake.custom_fake = tx_acked;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));

	radio_tx_fake.custom_fake = NULL;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));

	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_NO_ACK);
	zassert_false(zzb_radio_tx_done_fake.arg3_val);
}

static void init_without_hw_csma(void)
{
	radio_get_caps_fake.return_val = IEEE802154_HW_TX_RX_ACK;
	zassert_ok(zzb_radio_init(&port, DEVICE_GET(test_radio)));
}

ZTEST(zzb_radio_raw, test_sw_csma_ca_retries_cca_then_sends_direct)
{
	int cca_results[] = {-EBUSY, -EBUSY, 0};

	init_without_hw_csma();
	SET_RETURN_SEQ(radio_cca, cca_results, ARRAY_SIZE(cca_results));

	transmit(tx_buf(bcast_frame, sizeof(bcast_frame)));

	zassert_equal(radio_cca_fake.call_count, 3);
	zassert_equal(radio_tx_fake.call_count, 1);
	zassert_equal(radio_tx_fake.arg1_val, IEEE802154_TX_MODE_DIRECT);
	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_SUCCESS);
}

ZTEST(zzb_radio_raw, test_sw_csma_ca_gives_up_after_max_backoffs)
{
	init_without_hw_csma();
	ack[0] = 0x12;
	radio_tx_fake.custom_fake = tx_acked;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));

	RESET_FAKE(radio_cca);
	radio_cca_fake.return_val = -EBUSY;
	transmit(tx_buf(unicast_frame, sizeof(unicast_frame)));

	/* macMaxCSMABackoffs + 1 */
	zassert_equal(radio_cca_fake.call_count, 5);
	zassert_equal(radio_tx_fake.call_count, 1);
	zassert_equal(zzb_radio_tx_done_fake.arg2_val, ZZB_MAC_STATUS_CHANNEL_ACCESS_FAILURE);
	zassert_false(zzb_radio_tx_done_fake.arg3_val);
}

/* k_cycle_get_64() at the last tx(); native_sim counts cycles in microseconds */
static uint64_t tx_cycles;

static int tx_record_time(const struct device *dev, enum ieee802154_tx_mode mode,
			  struct net_pkt *pkt, struct net_buf *frag)
{
	tx_cycles = k_cycle_get_64();
	return 0;
}

/*
 * CONFIG_ZZB_RADIO_TX_BACKOFF: before the CSMA-CA of the driver the port waits 0 to 7 unit backoff
 * periods of 320 us, as for esp32, whose CSMA-CA does one CCA without backoff
 */
ZTEST(zzb_radio_raw, test_tx_backs_off_before_hardware_csma_ca)
{
	uint8_t waits = 0;

	if (!IS_ENABLED(CONFIG_ZZB_RADIO_TX_BACKOFF)) {
		ztest_test_skip();
	}

	radio_tx_fake.custom_fake = tx_record_time;
	for (int i = 0; i < 32; i++) {
		uint64_t start = k_cycle_get_64();
		uint64_t wait;

		transmit(tx_buf(bcast_frame, sizeof(bcast_frame)));
		wait = tx_cycles - start;
		zassert_true(wait <= 7 * 320, "waited %llu us", wait);
		waits |= BIT(wait / 320);
	}

	zassert_equal(radio_cca_fake.call_count, 0);
	zassert_equal(radio_tx_fake.arg1_val, IEEE802154_TX_MODE_CSMA_CA);
	/* More than one bit: the waits differ */
	zassert_not_equal(waits & (waits - 1), 0, "one backoff for all frames");
}

ZTEST_SUITE(zzb_radio_raw, NULL, NULL, before, NULL, NULL);
