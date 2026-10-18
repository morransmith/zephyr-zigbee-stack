/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_mcps_data_req, struct zzb_node *, struct net_buf *);
DEFINE_FAKE_VOID_FUNC(zzb_nlde_data_conf, struct zzb_node *, uint8_t, enum zzb_mac_status);
DEFINE_FAKE_VOID_FUNC(zzb_nlde_data_ind, struct zzb_node *, struct net_buf *, uint16_t, uint16_t,
		      bool);
DEFINE_FAKE_VALUE_FUNC(int, zzb_mlme_scan_req, struct zzb_node *, uint32_t, uint8_t);
DEFINE_FAKE_VOID_FUNC(zzb_nlme_network_discovery_conf, struct zzb_node *, uint8_t,
		      const struct zzb_nwk_network *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, zzb_mlme_assoc_req, struct zzb_node *, uint8_t,
		       const struct zzb_mac_addr *, uint8_t);
DEFINE_FAKE_VALUE_FUNC(uint64_t, zzb_mlme_get_coord_ext_addr, const struct zzb_node *);
DEFINE_FAKE_VOID_FUNC(zzb_nlme_join_conf, struct zzb_node *, uint8_t);

struct frame_copy sent[4];
struct frame_copy data_ind;
struct zzb_mac_addr assoc_coord;

static void copy_frame(struct frame_copy *copy, struct net_buf *buf)
{
	zassert_true(buf->len <= sizeof(copy->data));
	memcpy(copy->data, buf->data, buf->len);
	copy->len = buf->len;
	copy->meta = *zzb_frame_meta(buf);
}

static int mcps_data_req_take(struct zzb_node *node, struct net_buf *buf)
{
	zassert_true(zzb_mcps_data_req_fake.call_count <= ARRAY_SIZE(sent));
	copy_frame(&sent[zzb_mcps_data_req_fake.call_count - 1], buf);
	net_buf_unref(buf);
	return 0;
}

static int assoc_req_copy(struct zzb_node *node, uint8_t channel, const struct zzb_mac_addr *coord,
			  uint8_t capability)
{
	assoc_coord = *coord;
	return zzb_mlme_assoc_req_fake.return_val;
}

static void nlde_data_ind_take(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
			       uint16_t src_addr, bool security_use)
{
	copy_frame(&data_ind, buf);
	net_buf_unref(buf);
}

struct zzb_node *test_node(int index)
{
	static const uint64_t eui64[] = {NODE_EUI64, SENDER_EUI64};
	static struct zzb_node *nodes[ARRAY_SIZE(eui64)];

	if (nodes[0] == NULL) {
		ARRAY_FOR_EACH(nodes, i) {
			const struct zzb_node_config cfg = {
				.eui64 = eui64[i],
			};

			zassert_ok(zzb_node_init(&cfg, &nodes[i]));
		}
	}

	return nodes[index];
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_mlme_set_pan_id);
	RESET_FAKE(zzb_mlme_set_short_addr);
	RESET_FAKE(zzb_mcps_data_req);
	RESET_FAKE(zzb_nlde_data_conf);
	RESET_FAKE(zzb_nlde_data_ind);
	RESET_FAKE(zzb_mlme_scan_req);
	RESET_FAKE(zzb_nlme_network_discovery_conf);
	RESET_FAKE(zzb_mlme_assoc_req);
	RESET_FAKE(zzb_mlme_get_coord_ext_addr);
	RESET_FAKE(zzb_nlme_join_conf);
	FFF_RESET_HISTORY();

	zzb_mcps_data_req_fake.custom_fake = mcps_data_req_take;
	zzb_nlde_data_ind_fake.custom_fake = nlde_data_ind_take;
	zzb_mlme_assoc_req_fake.custom_fake = assoc_req_copy;
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

void notify_beacon(struct zzb_node *node, const struct zzb_nwk_beacon *beacon, uint16_t pan_id,
		   uint16_t short_addr, uint8_t channel, uint16_t superframe, uint8_t lqi)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct zzb_frame_meta *meta;

	zassert_not_null(buf);
	zzb_nwk_beacon_write(beacon, net_buf_add(buf, ZZB_NWK_BEACON_LEN));
	meta = zzb_frame_meta(buf);
	meta->src = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_SHORT,
		.pan_id = pan_id,
		.short_addr = short_addr,
	};
	meta->lqi = lqi;
	zzb_mlme_beacon_notify_ind(node, buf, channel, superframe);
}
