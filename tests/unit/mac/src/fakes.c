/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "mac/mac.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(enum ieee802154_hw_caps, radio_get_caps, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, radio_start, const struct device *);
FAKE_VALUE_FUNC(int, radio_stop, const struct device *);
FAKE_VALUE_FUNC(int, radio_set_channel, const struct device *, uint16_t);
FAKE_VALUE_FUNC(int, radio_filter, const struct device *, bool, enum ieee802154_filter_type,
		const struct ieee802154_filter *);
FAKE_VALUE_FUNC(int, radio_tx, const struct device *, enum ieee802154_tx_mode, struct net_pkt *,
		struct net_buf *);
FAKE_VALUE_FUNC(int, radio_configure, const struct device *, enum ieee802154_config_type,
		const struct ieee802154_config *);
DEFINE_FAKE_VOID_FUNC(zzb_mcps_data_conf, struct zzb_node *, uint8_t, enum zzb_mac_status);
DEFINE_FAKE_VOID_FUNC(zzb_mcps_data_ind, struct zzb_node *, struct net_buf *);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_scan_conf, struct zzb_node *, enum zzb_mac_status);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_poll_conf, struct zzb_node *, enum zzb_mac_status);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_assoc_conf, struct zzb_node *, uint16_t, enum zzb_mac_status);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_beacon_notify_ind, struct zzb_node *, struct net_buf *, uint8_t,
		      uint16_t);

static const struct ieee802154_radio_api radio_api = {
	.get_capabilities = radio_get_caps,
	.start = radio_start,
	.stop = radio_stop,
	.set_channel = radio_set_channel,
	.filter = radio_filter,
	.tx = radio_tx,
	.configure = radio_configure,
};

DEVICE_DEFINE(test_radio, "test_radio", NULL, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &radio_api);

const struct device *const radio_dev = DEVICE_GET(test_radio);
struct radio_state radio;
struct sent_frame sent[8];
int sent_count;
struct parent parent;
struct zzb_node *node;
struct rx_copy data_ind;
struct rx_copy beacon;

static void copy_rx(struct rx_copy *copy, struct net_buf *buf)
{
	memcpy(copy->data, buf->data, buf->len);
	copy->len = buf->len;
	copy->meta = *zzb_frame_meta(buf);
	net_buf_unref(buf);
}

static void data_ind_fake(struct zzb_node *n, struct net_buf *buf)
{
	copy_rx(&data_ind, buf);
}

static void beacon_notify_ind_fake(struct zzb_node *n, struct net_buf *buf, uint8_t channel,
				   uint16_t superframe)
{
	copy_rx(&beacon, buf);
}

static int start_fake(const struct device *dev)
{
	radio.started = true;
	return 0;
}

static int stop_fake(const struct device *dev)
{
	radio.started = false;
	return 0;
}

static int configure_fake(const struct device *dev, enum ieee802154_config_type type,
			  const struct ieee802154_config *config)
{
	if (type != IEEE802154_CONFIG_RX_ON_WHEN_IDLE) {
		return -ENOTSUP;
	}

	radio.rx_on_when_idle = config->rx_on_when_idle;
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

static void send_ack(uint8_t seq)
{
	const uint8_t ack[] = {parent.pending ? 0x12 : 0x02, 0x00, seq};
	struct net_pkt *pkt =
		net_pkt_rx_alloc_with_buffer(NULL, sizeof(ack), NET_AF_UNSPEC, 0, K_NO_WAIT);

	zassert_not_null(pkt);
	zassert_ok(net_pkt_write(pkt, ack, sizeof(ack)));
	ieee802154_handle_ack(NULL, pkt);
	net_pkt_unref(pkt);
}

static bool reply_due(struct net_buf *frag)
{
	struct zzb_mac_hdr hdr;
	int len = zzb_mac_hdr_parse(frag->data, frag->len, &hdr);

	return parent.reply != NULL && len > 0 && hdr.type == ZZB_MAC_FRAME_CMD &&
	       frag->data[len] == parent.reply_to &&
	       (parent.reply_channel == 0 || parent.reply_channel == radio.channel);
}

static int tx_fake(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		   struct net_buf *frag)
{
	struct sent_frame *s;

	zassert_true(sent_count < ARRAY_SIZE(sent));
	s = &sent[sent_count++];
	memcpy(s->data, frag->data, frag->len);
	s->len = frag->len;
	s->radio = radio;
	s->pm_locked = pm_locked();

	if (radio.channel == parent.busy_channel) {
		return -EBUSY;
	}

	if (ieee802154_is_ar_flag_set(frag)) {
		if (parent.lost > 0) {
			parent.lost--;
			return -ENOMSG;
		}

		send_ack(frag->data[2]);
	}

	if (reply_due(frag)) {
		inject(parent.reply, parent.reply_len);
	}

	return 0;
}

static void reset_fakes(void)
{
	RESET_FAKE(radio_get_caps);
	RESET_FAKE(radio_start);
	RESET_FAKE(radio_stop);
	RESET_FAKE(radio_set_channel);
	RESET_FAKE(radio_filter);
	RESET_FAKE(radio_tx);
	RESET_FAKE(radio_configure);
	RESET_FAKE(zzb_mcps_data_conf);
	RESET_FAKE(zzb_mcps_data_ind);
	RESET_FAKE(zzb_mlme_scan_conf);
	RESET_FAKE(zzb_mlme_poll_conf);
	RESET_FAKE(zzb_mlme_assoc_conf);
	RESET_FAKE(zzb_mlme_beacon_notify_ind);
	FFF_RESET_HISTORY();
	radio_get_caps_fake.return_val = IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_CSMA;
	radio_start_fake.custom_fake = start_fake;
	radio_stop_fake.custom_fake = stop_fake;
	radio_set_channel_fake.custom_fake = set_channel_fake;
	radio_filter_fake.custom_fake = filter_fake;
	radio_tx_fake.custom_fake = tx_fake;
	radio_configure_fake.custom_fake = configure_fake;
	zzb_mcps_data_ind_fake.custom_fake = data_ind_fake;
	zzb_mlme_beacon_notify_ind_fake.custom_fake = beacon_notify_ind_fake;
	/* nrf5 and esp32 start with RxOnWhenIdle TRUE */
	radio = (struct radio_state){.rx_on_when_idle = true};
	sent_count = 0;
	parent = (struct parent){0};
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
	node->sleepy = false;
	zassert_ok(zzb_mac_init(node, radio_dev));
}

void mac_init_sleepy(void)
{
	node->sleepy = true;
	radio_get_caps_fake.return_val |= IEEE802154_RX_ON_WHEN_IDLE;
	zassert_ok(zzb_mac_init(node, radio_dev));
}

void mac_after(void *fixture)
{
	struct net_buf *bufs[CONFIG_ZZB_BUF_COUNT];

	zassert_equal(node->mac.op, ZZB_MAC_OP_NONE);
	zassert_false(k_work_delayable_is_pending(&node->mac.timer));
	zassert_true(k_fifo_is_empty(&node->mac.tx_fifo));
	if (!node->sleepy) {
		zassert_equal(radio_configure_fake.call_count, 0);
		zassert_equal(radio_stop_fake.call_count, 0);
	}

	zassert_equal(pm_locked(), IS_ENABLED(CONFIG_PM) && !node->sleepy);

	ARRAY_FOR_EACH(bufs, i) {
		bufs[i] = zzb_buf_alloc(K_NO_WAIT);
		zassert_not_null(bufs[i], "buffer leak");
	}

	ARRAY_FOR_EACH(bufs, i) {
		net_buf_unref(bufs[i]);
	}
}

bool pm_locked(void)
{
	return pm_policy_state_lock_is_active(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
}

void run_stack(void)
{
	/* The test thread is cooperative and has a lower priority than the stack thread */
	k_yield();
}

void inject(const uint8_t *frame, size_t len)
{
	struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(NULL, len, NET_AF_UNSPEC, 0, K_NO_WAIT);

	zassert_not_null(pkt);
	zassert_ok(net_pkt_write(pkt, frame, len));
	zassert_ok(net_recv_data(NULL, pkt));
}

struct net_buf *msdu_buf(uint8_t handle, const uint8_t *msdu, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct zzb_frame_meta *meta;

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_MAC_HDR_MAX_LEN);
	net_buf_add_mem(buf, msdu, len);

	meta = zzb_frame_meta(buf);
	meta->dst = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = PAN_ID,
		.short_addr = PARENT_SHORT,
	};
	meta->flags = ZZB_FRAME_ACK_REQ;
	meta->handle = handle;
	return buf;
}

void check_sent(int i, const uint8_t *frame, size_t len)
{
	zassert_true(i < sent_count, "frame %d not sent", i);
	zassert_equal(sent[i].len, len, "frame %d", i);
	zassert_mem_equal(sent[i].data, frame, 2, "frame %d", i);
	zassert_mem_equal(sent[i].data + 3, frame + 3, len - 3, "frame %d", i);
}
