/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(bool, zzb_nwk_key_pending, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_nlde_data_req, struct zzb_node *, struct net_buf *, uint16_t,
		       uint8_t, bool);
DEFINE_FAKE_VALUE_FUNC(struct zzb_nwk_neighbor *, zzb_nwk_neighbor_find_short, struct zzb_node *,
		       uint16_t);
DEFINE_FAKE_VALUE_FUNC(struct zzb_nwk_neighbor *, zzb_nwk_neighbor_find_ext, struct zzb_node *,
		       uint64_t);
DEFINE_FAKE_VOID_FUNC(zzb_apsde_data_conf, struct zzb_node *, uint8_t, uint8_t);
DEFINE_FAKE_VOID_FUNC(zzb_apsde_data_ind, struct zzb_node *, struct net_buf *,
		      const struct zzb_apsde_ind *);

struct frame_copy sent[8];
struct frame_copy data_ind;
struct zzb_apsde_ind ind;

const uint8_t key_b[ZZB_SEC_KEY_LEN] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static void copy_frame(struct frame_copy *copy, struct net_buf *buf)
{
	zassert_true(buf->len <= sizeof(copy->data));
	memcpy(copy->data, buf->data, buf->len);
	copy->len = buf->len;
	copy->meta = *zzb_frame_meta(buf);
}

static int nlde_data_req_take(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
			      uint8_t radius, bool secure)
{
	zassert_true(zzb_nlde_data_req_fake.call_count <= ARRAY_SIZE(sent));
	copy_frame(&sent[zzb_nlde_data_req_fake.call_count - 1], buf);
	net_buf_unref(buf);
	return 0;
}

static void apsde_data_ind_take(struct zzb_node *node, struct net_buf *buf,
				const struct zzb_apsde_ind *params)
{
	copy_frame(&data_ind, buf);
	ind = *params;
	net_buf_unref(buf);
}

struct zzb_node *test_node(int index)
{
	static const uint64_t eui64[] = {NODE_EUI64, TC_EUI64};
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
	RESET_FAKE(zzb_nwk_key_pending);
	RESET_FAKE(zzb_nlde_data_req);
	RESET_FAKE(zzb_nwk_neighbor_find_short);
	RESET_FAKE(zzb_nwk_neighbor_find_ext);
	RESET_FAKE(zzb_apsde_data_conf);
	RESET_FAKE(zzb_apsde_data_ind);
	FFF_RESET_HISTORY();

	zzb_nlde_data_req_fake.custom_fake = nlde_data_req_take;
	zzb_apsde_data_ind_fake.custom_fake = apsde_data_ind_take;
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

/* The test thread is cooperative and has a lower priority than the stack thread */
void run_stack(void)
{
	k_yield();
}
