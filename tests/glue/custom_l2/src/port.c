/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "fake_radio.h"
#include "mac/mac.h"
#include "radio/port.h"

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(zzb_radio_rx, struct zzb_radio_port *, struct net_buf *);
FAKE_VOID_FUNC(zzb_radio_tx_done, struct zzb_radio_port *, struct net_buf *, enum zzb_mac_status,
	       bool);

static struct zzb_radio_port ports[FAKE_RADIO_COUNT];

/* Data frame with ACK request from 0x1234 to 0x0000 in PAN 0x1a62, one payload byte */
static const uint8_t unicast_frame[] = {0x61, 0x88, 0x01, 0x62, 0x1a, 0x00, 0x00, 0x34, 0x12, 0x42};

static uint8_t rx_data[ZZB_BUF_SIZE];
static size_t rx_len;

static void rx_fake(struct zzb_radio_port *port, struct net_buf *buf)
{
	memcpy(rx_data, buf->data, buf->len);
	rx_len = buf->len;
	net_buf_unref(buf);
}

static void tx_done_fake(struct zzb_radio_port *port, struct net_buf *buf,
			 enum zzb_mac_status status, bool frame_pending)
{
	net_buf_unref(buf);
}

/* Sends frame from port 0 and returns the status of its confirm */
static enum zzb_mac_status transmit(const uint8_t *frame, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct k_work_sync sync;

	zassert_not_null(buf);
	net_buf_add_mem(buf, frame, len);
	zassert_ok(zzb_radio_tx(&ports[0], buf));
	k_work_flush(&ports[0].tx_work, &sync);
	k_work_flush(&ports[1].rx_work, &sync);

	zassert_equal(zzb_radio_tx_done_fake.call_count, 1);
	return zzb_radio_tx_done_fake.arg2_val;
}

static void set_filter(enum ieee802154_filter_type type, const struct ieee802154_filter *filter)
{
	const struct ieee802154_radio_api *api = fake_radios[1]->api;

	zassert_ok(api->filter(fake_radios[1], true, type, filter));
}

static void before(void *fixture)
{
	RESET_FAKE(zzb_radio_rx);
	RESET_FAKE(zzb_radio_tx_done);
	zzb_radio_rx_fake.custom_fake = rx_fake;
	zzb_radio_tx_done_fake.custom_fake = tx_done_fake;

	ARRAY_FOR_EACH(ports, i) {
		zassert_ok(zzb_radio_init(&ports[i], fake_radios[i]));
	}

	fake_radio_reset();
	set_filter(IEEE802154_FILTER_TYPE_PAN_ID, &(struct ieee802154_filter){.pan_id = 0x1a62});
	set_filter(IEEE802154_FILTER_TYPE_SHORT_ADDR, &(struct ieee802154_filter){.short_addr = 0});
}

ZTEST(zzb_radio_custom_l2, test_unicast_is_received_and_acked)
{
	zassert_equal(transmit(unicast_frame, sizeof(unicast_frame)), ZZB_MAC_STATUS_SUCCESS);

	zassert_equal(zzb_radio_rx_fake.call_count, 1);
	zassert_equal_ptr(zzb_radio_rx_fake.arg0_val, &ports[1]);
	zassert_equal(rx_len, sizeof(unicast_frame));
	zassert_mem_equal(rx_data, unicast_frame, rx_len);
}

ZTEST(zzb_radio_custom_l2, test_link_down_gives_no_ack)
{
	fake_radio_set_link(0, 1, false);

	zassert_equal(transmit(unicast_frame, sizeof(unicast_frame)), ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_radio_rx_fake.call_count, 0);
}

ZTEST(zzb_radio_custom_l2, test_frame_for_other_address_gives_no_ack)
{
	set_filter(IEEE802154_FILTER_TYPE_SHORT_ADDR,
		   &(struct ieee802154_filter){.short_addr = 0x5678});

	zassert_equal(transmit(unicast_frame, sizeof(unicast_frame)), ZZB_MAC_STATUS_NO_ACK);
	zassert_equal(zzb_radio_rx_fake.call_count, 0);
}

ZTEST(zzb_radio_custom_l2, test_eui64_is_read_from_link_address)
{
	uint64_t eui64;

	zassert_ok(zzb_radio_eui64(fake_radios[0], &eui64));
	zassert_equal(eui64, 0x0011223344556677ULL);
}

ZTEST_SUITE(zzb_radio_custom_l2, NULL, NULL, before, NULL, NULL);
