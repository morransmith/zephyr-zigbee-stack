/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/byteorder.h>

#include "fake_radio.h"
#include "mac/frame.h"

LOG_MODULE_REGISTER(fake_radio, LOG_LEVEL_DBG);

struct fake_radio {
	struct net_if *iface;
	uint8_t eui64[8];
	uint64_t ext_addr;
	uint16_t short_addr;
	uint16_t pan_id;
};

static struct fake_radio radios[FAKE_RADIO_COUNT] = {
	{.eui64 = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}},
	{.eui64 = {0, 0, 0, 0, 0, 0, 0, 2}},
};

/* link_down[from][to] */
static bool link_down[FAKE_RADIO_COUNT][FAKE_RADIO_COUNT];

void fake_radio_reset(void)
{
	memset(link_down, 0, sizeof(link_down));
	ARRAY_FOR_EACH_PTR(radios, radio) {
		radio->ext_addr = 0;
		radio->short_addr = 0xfffe;
		radio->pan_id = 0xffff;
	}
}

void fake_radio_set_link(int from, int to, bool up)
{
	link_down[from][to] = !up;
}

static void iface_init(struct net_if *iface)
{
	struct fake_radio *radio = net_if_get_device(iface)->data;

	radio->iface = iface;
	/* net_if_up() asserts that the interface has a link address */
	net_if_set_link_addr(iface, radio->eui64, sizeof(radio->eui64), NET_LINK_IEEE802154);
	ieee802154_init(iface);
}

static enum ieee802154_hw_caps get_capabilities(const struct device *dev)
{
	return IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_RX_TX_ACK | IEEE802154_HW_FILTER |
	       IEEE802154_HW_CSMA;
}

static int start(const struct device *dev)
{
	return 0;
}

/* The medium has one channel */
static int set_channel(const struct device *dev, uint16_t channel)
{
	return 0;
}

static int filter(const struct device *dev, bool set, enum ieee802154_filter_type type,
		  const struct ieee802154_filter *filter)
{
	struct fake_radio *radio = dev->data;

	if (!set) {
		return -ENOTSUP;
	}

	switch (type) {
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		radio->ext_addr = sys_get_le64(filter->ieee_addr);
		return 0;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		radio->short_addr = filter->short_addr;
		return 0;
	case IEEE802154_FILTER_TYPE_PAN_ID:
		radio->pan_id = filter->pan_id;
		return 0;
	default:
		return -ENOTSUP;
	}
}

/* Destination address rules of the third-level filtering (802.15.4 5.1.6.2) */
static bool accepts(const struct fake_radio *radio, struct net_buf *frag)
{
	struct zzb_mac_hdr hdr;

	if (zzb_mac_hdr_parse(frag->data, frag->len, &hdr) < 0) {
		return false;
	}

	if (hdr.dst.mode == ZZB_MAC_ADDR_NONE) {
		return true;
	}

	if (hdr.dst.pan_id != 0xffff && hdr.dst.pan_id != radio->pan_id) {
		return false;
	}

	if (hdr.dst.mode == ZZB_MAC_ADDR_SHORT) {
		return hdr.dst.short_addr == 0xffff || hdr.dst.short_addr == radio->short_addr;
	}

	return hdr.dst.ext_addr == radio->ext_addr;
}

static struct net_pkt *alloc_pkt(struct fake_radio *radio, const uint8_t *data, size_t len)
{
	struct net_pkt *pkt =
		net_pkt_rx_alloc_with_buffer(radio->iface, len, NET_AF_UNSPEC, 0, K_NO_WAIT);

	__ASSERT_NO_MSG(pkt != NULL);
	net_pkt_write(pkt, data, len);
	return pkt;
}

static void deliver(struct fake_radio *radio, struct net_buf *frag)
{
	struct net_pkt *pkt = alloc_pkt(radio, frag->data, frag->len);

	if (net_recv_data(radio->iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
}

static void send_ack(struct fake_radio *radio, uint8_t seq)
{
	const uint8_t ack[] = {0x02, 0x00, seq};
	struct net_pkt *pkt = alloc_pkt(radio, ack, sizeof(ack));

	ieee802154_handle_ack(radio->iface, pkt);
	net_pkt_unref(pkt);
}

static int fake_tx(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		   struct net_buf *frag)
{
	struct fake_radio *src = dev->data;
	int from = src - radios;
	bool received = false;

	LOG_HEXDUMP_DBG(frag->data, frag->len, dev->name);

	ARRAY_FOR_EACH(radios, to) {
		if (to == from || link_down[from][to] || !accepts(&radios[to], frag)) {
			continue;
		}

		deliver(&radios[to], frag);
		received = true;
	}

	if (!ieee802154_is_ar_flag_set(frag)) {
		return 0;
	}

	if (!received) {
		return -ENOMSG;
	}

	send_ack(src, frag->data[2]);
	return 0;
}

static const struct ieee802154_radio_api api = {
	.iface_api.init = iface_init,
	.get_capabilities = get_capabilities,
	.start = start,
	.set_channel = set_channel,
	.filter = filter,
	.tx = fake_tx,
};

NET_DEVICE_INIT(fake_radio_0, "fake_radio_0", NULL, NULL, &radios[0], NULL,
		CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api, CUSTOM_IEEE802154_L2,
		NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2), CONFIG_NET_L2_CUSTOM_IEEE802154_MTU);
NET_DEVICE_INIT(fake_radio_1, "fake_radio_1", NULL, NULL, &radios[1], NULL,
		CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api, CUSTOM_IEEE802154_L2,
		NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2), CONFIG_NET_L2_CUSTOM_IEEE802154_MTU);

const struct device *const fake_radios[] = {
	DEVICE_GET(fake_radio_0),
	DEVICE_GET(fake_radio_1),
};
