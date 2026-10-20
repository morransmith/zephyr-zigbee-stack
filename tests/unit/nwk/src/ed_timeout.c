/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

/* Node 0 is the child of node 1 */
static struct zzb_node *child;
static struct zzb_node *parent;

static void *setup(void)
{
	child = test_node(0);
	parent = test_node(1);
	return NULL;
}

static void before(void *fixture)
{
	delete_nwk_records();
	reset_fakes();
	pair_nodes(child, parent);
}

static void after(void *fixture)
{
	child->sleepy = false;
	check_no_buffer_leak();
}

/* The network key arrives after an association join */
static void receive_key(void)
{
	child->nwk.key_pending = true;
	zzb_nlme_set_nwk_key(child, test_key, 0);
	run_stack();
}

static void receive_rsp(uint8_t status, uint8_t parent_info, uint64_t src_ext)
{
	const struct zzb_nwk_hdr hdr = {
		.type = ZZB_NWK_FRAME_CMD,
		.flags = ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE,
		.dst = CHILD_SHORT,
		.src = PARENT_SHORT,
		.radius = 1,
		.dst_ext = child->eui64,
		.src_ext = src_ext,
	};
	const struct zzb_nwk_cmd cmd = {
		.id = ZZB_NWK_CMD_ED_TIMEOUT_RSP,
		.ed_timeout_rsp = {.status = status, .parent_info = parent_info},
	};

	receive_cmd(child, parent, &hdr, &cmd);
}

/* R22 3.6.10.2 and 3.4.11 */
ZTEST(zzb_nwk_ed_timeout, test_request_follows_the_network_key)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	receive_key();
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
	zassert_equal(sent[0].meta.dst.short_addr, PARENT_SHORT);
	zassert_equal(sent[0].meta.flags, ZZB_FRAME_ACK_REQ);
	zassert_equal(sent[0].meta.handle, 0xf0 | ZZB_NWK_CMD_ED_TIMEOUT_REQ);

	parse_sent_cmd(0, parent, &hdr, &cmd);
	zassert_equal(hdr.type, ZZB_NWK_FRAME_CMD);
	zassert_equal(hdr.flags, ZZB_NWK_SECURITY | ZZB_NWK_SRC_IEEE | ZZB_NWK_DST_IEEE);
	zassert_equal(hdr.dst, PARENT_SHORT);
	zassert_equal(hdr.src, CHILD_SHORT);
	zassert_equal(hdr.radius, 1);
	zassert_equal(hdr.src_ext, child->eui64);
	zassert_equal(hdr.dst_ext, parent->eui64);
	zassert_equal(cmd.id, ZZB_NWK_CMD_ED_TIMEOUT_REQ);
	zassert_equal(cmd.ed_timeout_req.timeout, CONFIG_ZZB_NWK_ED_TIMEOUT);
	zassert_equal(cmd.ed_timeout_req.config, 0);
}

/* The scheduler polls macResponseWaitTime after the request instead of the long interval */
ZTEST(zzb_nwk_ed_timeout, test_sleepy_node_polls_for_the_response)
{
	child->sleepy = true;
	zzb_nwk_poll_start(child);
	receive_key();
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);

	k_sleep(K_MSEC(400));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zassert_equal(poll_coord.short_addr, PARENT_SHORT);
}

/* A key update on the network sends no request */
ZTEST(zzb_nwk_ed_timeout, test_no_request_without_a_pending_key)
{
	zzb_nlme_set_nwk_key(child, test_key, 1);
	run_stack();
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

ZTEST(zzb_nwk_ed_timeout, test_request_is_retried_without_a_buffer)
{
	struct net_buf *bufs[CONFIG_ZZB_BUF_COUNT];

	ARRAY_FOR_EACH(bufs, i) {
		bufs[i] = zzb_buf_alloc(K_NO_WAIT);
	}

	receive_key();
	ARRAY_FOR_EACH(bufs, i) {
		net_buf_unref(bufs[i]);
	}

	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(900));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
}

/* R22 3.6.10.2: the parent information sets the end device initiator bit (3.3.1.1.9) */
ZTEST(zzb_nwk_ed_timeout, test_response_sets_the_parent_information)
{
	static const uint8_t nsdu[] = {0x42};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);
	struct zzb_nwk_hdr hdr;

	uint8_t rec[64];

	receive_rsp(0x00, 0x03, parent->eui64);
	zassert_equal(child->nwk.nib.parent_info, 0x03);
	zassert_equal(load_record("nwk/net", rec, sizeof(rec)), 28);
	zassert_equal(rec[NET_PARENT_INFO], 0x03);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);
	net_buf_add_mem(buf, nsdu, sizeof(nsdu));
	zassert_ok(zzb_nlde_data_req(child, buf, PARENT_SHORT, 0, true));
	zassert_true(zzb_nwk_hdr_parse(sent[0].data, sent[0].len, &hdr) > 0);
	zassert_true(hdr.flags & ZZB_NWK_ED_INITIATOR);
}

ZTEST(zzb_nwk_ed_timeout, test_other_responses_are_ignored)
{
	/* INCORRECT_VALUE, and a source IEEE address other than the parent's */
	receive_rsp(0x01, 0x03, parent->eui64);
	receive_rsp(0x00, 0x03, 0x0102030405060708ULL);
	zassert_equal(child->nwk.nib.parent_info, 0);
}

/* Timeout / 3 for the 10 s of CONFIG_ZZB_NWK_ED_TIMEOUT=0 (R22 3.6.10.3) */
#define PERIOD_MS 3333

/* R22 3.6.10.3: bit 0 of the parent information selects MAC data poll keepalives */
ZTEST(zzb_nwk_ed_timeout, test_keepalive_polls_the_parent)
{
	receive_rsp(0x00, 0x03, parent->eui64);

	k_sleep(K_MSEC(PERIOD_MS - 50));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zassert_equal_ptr(zzb_mlme_poll_req_fake.arg0_val, child);
	zassert_equal(poll_coord.mode, ZZB_MAC_ADDR_SHORT);
	zassert_equal(poll_coord.pan_id, PAN_ID);
	zassert_equal(poll_coord.short_addr, PARENT_SHORT);

	k_sleep(K_MSEC(PERIOD_MS));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

/* A poll that cannot start is tried again after 1 s */
ZTEST(zzb_nwk_ed_timeout, test_busy_poll_is_retried)
{
	zzb_mlme_poll_req_fake.return_val = -EBUSY;
	receive_rsp(0x00, 0x01, parent->eui64);

	k_sleep(K_MSEC(PERIOD_MS + 50));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zzb_mlme_poll_req_fake.return_val = 0;
	k_sleep(K_MSEC(1000));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
	k_sleep(K_MSEC(PERIOD_MS - 100));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 2);
}

/* With bit 1 the keepalive is an End Device Timeout Request answered in macResponseWaitTime */
ZTEST(zzb_nwk_ed_timeout, test_keepalive_without_response_reports_parent_link_failure)
{
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;

	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(PERIOD_MS + 50));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
	parse_sent_cmd(0, parent, &hdr, &cmd);
	zassert_equal(cmd.id, ZZB_NWK_CMD_ED_TIMEOUT_REQ);
	zassert_true(hdr.flags & ZZB_NWK_ED_INITIATOR);

	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(PERIOD_MS));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 2);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);

	/* macResponseWaitTime, 491.52 ms */
	k_sleep(K_MSEC(500));
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x09);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg2_val, PARENT_SHORT);

	/* The keepalive has stopped */
	k_sleep(K_MSEC(2 * PERIOD_MS));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 2);
}

/* A response 1.5 x macResponseWaitTime after the request, fetched by the poll, is accepted */
ZTEST(zzb_nwk_ed_timeout, test_sleepy_keepalive_takes_a_response_after_the_poll)
{
	child->sleepy = true;
	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(PERIOD_MS + 50));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);

	k_sleep(K_MSEC(600));
	zassert_equal(zzb_mlme_poll_req_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
	k_sleep(K_MSEC(100));
	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(400));
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
}

ZTEST(zzb_nwk_ed_timeout, test_sleepy_keepalive_waits_twice_the_response_time)
{
	child->sleepy = true;
	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(PERIOD_MS + 50));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);

	/* 2 x macResponseWaitTime, 983.04 ms */
	k_sleep(K_MSEC(800));
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x09);
}

ZTEST(zzb_nwk_ed_timeout, test_failed_keepalive_response_reports_parent_link_failure)
{
	receive_rsp(0x00, 0x02, parent->eui64);
	k_sleep(K_MSEC(PERIOD_MS + 50));
	receive_rsp(0x01, 0x02, parent->eui64);

	zassert_equal(zzb_nlme_nwk_status_ind_fake.call_count, 1);
	zassert_equal(zzb_nlme_nwk_status_ind_fake.arg1_val, 0x09);
	k_sleep(K_MSEC(2 * PERIOD_MS));
	zassert_equal(zzb_mcps_data_req_fake.call_count, 1);
}

ZTEST(zzb_nwk_ed_timeout, test_no_keepalive_without_parent_support)
{
	receive_rsp(0x00, 0x00, parent->eui64);
	k_sleep(K_MSEC(2 * PERIOD_MS));

	zassert_equal(zzb_mlme_poll_req_fake.call_count, 0);
	zassert_equal(zzb_mcps_data_req_fake.call_count, 0);
}

ZTEST_SUITE(zzb_nwk_ed_timeout, NULL, setup, before, after, NULL);
