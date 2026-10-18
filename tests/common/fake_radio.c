/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>

#include "fake_radio.h"

LOG_MODULE_REGISTER(fake_radio, LOG_LEVEL_DBG);

struct fake_radio {
	struct net_if *iface;
	uint8_t eui64[8];
};

static struct fake_radio radios[FAKE_RADIO_COUNT] = {
	{.eui64 = {0, 0, 0, 0, 0, 0, 0, 1}},
	{.eui64 = {0, 0, 0, 0, 0, 0, 0, 2}},
};

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
	return IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_RX_TX_ACK | IEEE802154_HW_CSMA;
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
	bool received = false;

	LOG_HEXDUMP_DBG(frag->data, frag->len, dev->name);

	ARRAY_FOR_EACH_PTR(radios, dst) {
		if (dst == src) {
			continue;
		}

		deliver(dst, frag);
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
