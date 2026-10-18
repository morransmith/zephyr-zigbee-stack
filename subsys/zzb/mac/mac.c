/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "common/node.h"
#include "mac/mac.h"
#include "radio/port.h"

/* The first 2.4 GHz channel */
#define INIT_CHANNEL 11

static void set_filter(const struct device *dev, enum ieee802154_filter_type type,
		       struct ieee802154_filter filter)
{
	const struct ieee802154_radio_api *api = dev->api;

	api->filter(dev, true, type, &filter);
}

int zzb_mac_init(struct zzb_node *node, const struct device *radio)
{
	const struct ieee802154_radio_api *api = radio->api;
	uint8_t ext_addr[8];
	int err;

	err = zzb_radio_init(&node->port, radio);
	if (err) {
		return err;
	}

	node->mac = (struct zzb_mac){
		.pan_id = 0xffff,
		.short_addr = 0xffff,
		.dsn = sys_rand8_get(),
		.channel = INIT_CHANNEL,
	};
	k_fifo_init(&node->mac.tx_fifo);

	sys_put_le64(node->eui64, ext_addr);
	set_filter(radio, IEEE802154_FILTER_TYPE_IEEE_ADDR,
		   (struct ieee802154_filter){.ieee_addr = ext_addr});
	set_filter(radio, IEEE802154_FILTER_TYPE_PAN_ID,
		   (struct ieee802154_filter){.pan_id = node->mac.pan_id});
	set_filter(radio, IEEE802154_FILTER_TYPE_SHORT_ADDR,
		   (struct ieee802154_filter){.short_addr = node->mac.short_addr});
	api->set_channel(radio, node->mac.channel);

	err = api->start(radio);
	return err == -EALREADY ? 0 : err;
}

/* Frames are dropped until the MAC receives */
void zzb_radio_rx(struct zzb_radio_port *port, struct net_buf *buf)
{
	net_buf_unref(buf);
}
