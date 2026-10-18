/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/fff.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/mac.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(enum ieee802154_hw_caps, radio_get_caps, const struct device *);
FAKE_VALUE_FUNC(int, radio_start, const struct device *);
FAKE_VALUE_FUNC(int, radio_set_channel, const struct device *, uint16_t);
FAKE_VALUE_FUNC(int, radio_filter, const struct device *, bool, enum ieee802154_filter_type,
		const struct ieee802154_filter *);

static const struct ieee802154_radio_api radio_api = {
	.get_capabilities = radio_get_caps,
	.start = radio_start,
	.set_channel = radio_set_channel,
	.filter = radio_filter,
};

DEVICE_DEFINE(test_radio, "test_radio", NULL, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &radio_api);

const struct device *const radio_dev = DEVICE_GET(test_radio);
struct radio_state radio;
struct zzb_node *node;

static int start_fake(const struct device *dev)
{
	radio.started = true;
	return 0;
}

static int set_channel_fake(const struct device *dev, uint16_t channel)
{
	radio.channel = channel;
	return 0;
}

static int filter_fake(const struct device *dev, bool set, enum ieee802154_filter_type type,
		       const struct ieee802154_filter *filter)
{
	switch (type) {
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		radio.ext_addr = sys_get_le64(filter->ieee_addr);
		break;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		radio.short_addr = filter->short_addr;
		break;
	case IEEE802154_FILTER_TYPE_PAN_ID:
		radio.pan_id = filter->pan_id;
		break;
	default:
		break;
	}

	return 0;
}

static void reset_fakes(void)
{
	RESET_FAKE(radio_get_caps);
	RESET_FAKE(radio_start);
	RESET_FAKE(radio_set_channel);
	RESET_FAKE(radio_filter);
	FFF_RESET_HISTORY();
	radio_get_caps_fake.return_val = IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_CSMA;
	radio_start_fake.custom_fake = start_fake;
	radio_set_channel_fake.custom_fake = set_channel_fake;
	radio_filter_fake.custom_fake = filter_fake;
	radio = (struct radio_state){0};
}

void *mac_setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = NODE_EUI64,
		.radio = radio_dev,
	};

	/* CONFIG_ZZB_NODE_COUNT is 1, so the suites share one node */
	if (node == NULL) {
		reset_fakes();
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return NULL;
}

void mac_before(void *fixture)
{
	reset_fakes();
	zassert_ok(zzb_mac_init(node, radio_dev));
}
