/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "child.h"
#include "common/node.h"
#include "fake_radio.h"
#include "nwk/nwk.h"
#include "parent.h"

#define ROUTER_ADDR  0x2222
#define ROUTER_EUI64 0x00124b0000000002ULL

/* Receiver on when idle, allocate address */
#define CAPABILITY (ZZB_NWK_CAP_RX_ON_IDLE | ZZB_NWK_CAP_ALLOC_ADDR)

struct zzb_node *child;
struct child_log child_log;

/* NLME callbacks, which ZDO defines from milestone 10 */
static struct {
	int discovery_count;
	uint8_t discovery_status;
	struct zzb_nwk_network network;
	size_t network_count;
	int join_count;
	uint8_t join_status;
	int leave_count;
	uint8_t leave_status;
	int leave_ind_count;
	bool leave_ind_rejoin;
	int status_count;
	uint8_t status_code;
} nlme;

void zzb_nlme_network_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count)
{
	nlme.discovery_count++;
	nlme.discovery_status = status;
	nlme.network_count = count;
	if (count > 0) {
		nlme.network = networks[0];
	}
}

void zzb_nlme_join_conf(struct zzb_node *node, uint8_t status)
{
	nlme.join_count++;
	nlme.join_status = status;
}

void zzb_nlme_nwk_status_ind(struct zzb_node *node, uint8_t code, uint16_t addr)
{
	nlme.status_count++;
	nlme.status_code = code;
}

void zzb_nlme_leave_conf(struct zzb_node *node, uint8_t status)
{
	nlme.leave_count++;
	nlme.leave_status = status;
}

void zzb_nlme_leave_ind(struct zzb_node *node, uint64_t ext_addr, bool rejoin)
{
	nlme.leave_ind_count++;
	nlme.leave_ind_rejoin = rejoin;
}

/* APS callbacks, which ZDO defines from milestone 10 */
void zzb_apsme_transport_key_ind(struct zzb_node *node, uint64_t src_addr,
				 enum zzb_aps_key_type type, const uint8_t *key, uint8_t seq)
{
	child_log.transport_keys++;
	if (type == ZZB_APS_KEY_TC_LINK) {
		/* BDB 10.2.5 step 9 */
		zassert_ok(zzb_apsme_set_link_key(node, src_addr, key, ZZB_APS_KEY_UNVERIFIED,
						  ZZB_APS_LINK_KEY_UNIQUE));
		return;
	}

	zassert_ok(zzb_apsme_set_tc_addr(node, src_addr));
	zzb_nlme_set_nwk_key(node, key, seq);
}

void zzb_apsme_switch_key_ind(struct zzb_node *node, uint64_t src_addr, uint8_t seq)
{
	zassert_ok(zzb_nlme_set_active_key(node, seq));
}

void zzb_apsme_confirm_key_ind(struct zzb_node *node, uint8_t status, uint64_t src_addr,
			       enum zzb_aps_key_type type)
{
	child_log.confirm_keys++;
	child_log.confirm_status = status;
}

void zzb_apsde_data_ind(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_ind *ind)
{
	child_log.data_inds++;
	net_buf_unref(buf);
}

void zzb_apsde_data_conf(struct zzb_node *node, uint8_t handle, uint8_t status)
{
	child_log.data_confs++;
	child_log.data_status = status;
}

/* The trust center sends the network key; the child sets it and the trust center address */
static void receive_network_key(void)
{
	int count = child_log.transport_keys;

	parent_send_transport_key();
	k_sleep(K_MSEC(10));
	zassert_equal(child_log.transport_keys, count + 1);
	zassert_false(child->nwk.key_pending);
	zassert_equal(child->aps.aib.tc_addr, PARENT_EUI64);
}

/* Discovery on two channels, association, then the network key from the trust center */
static void join(void)
{
	zassert_ok(zzb_nlme_network_discovery_req(child, BIT(11) | BIT(CHANNEL), 0));

	/* Two channels of aBaseSuperframeDuration * 2, 30.72 ms each, in ticks of 10 ms */
	k_sleep(K_MSEC(200));
	zassert_equal(nlme.discovery_count, 1);
	zassert_equal(nlme.discovery_status, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(nlme.network_count, 1);
	zassert_equal(nlme.network.ext_pan_id, EXT_PAN_ID);
	zassert_equal(nlme.network.pan_id, PAN_ID);
	zassert_equal(nlme.network.channel, CHANNEL);
	zassert_true(nlme.network.permit_join);
	zassert_equal(parent_log.beacon_reqs, 1);

	zassert_ok(
		zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_ASSOC, 0, 0, CAPABILITY, false));

	/* macResponseWaitTime before the data request */
	k_sleep(K_MSEC(550));
	zassert_equal(nlme.join_count, 1);
	zassert_equal(nlme.join_status, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
	zassert_equal(zzb_nwk_neighbor_parent(child)->ext_addr, PARENT_EUI64);

	receive_network_key();
}

void child_init(void)
{
	const struct zzb_node_config cfg = {
		.radio = fake_radios[1],
	};

	if (child != NULL) {
		return;
	}

	fake_radio_reset();
	zassert_ok(zzb_node_init(&cfg, &child));
}

void child_join(void)
{
	memset(&nlme, 0, sizeof(nlme));
	memset(&child_log, 0, sizeof(child_log));
	parent_start(PARENT_ADDR, PARENT_EUI64, CHILD_ADDR, ZZB_NWK_PARENT_POLL_KEEPALIVE);
	zassert_ok(zzb_nlme_reset_req(child));
	zzb_aps_reset(child);
	join();
}

static void *setup(void)
{
	child_init();
	return NULL;
}

static void before(void *fixture)
{
	child_join();
}

/* R22 3.6.10.2: the request follows the network key */
ZTEST(zzb_nwk_join, test_end_device_timeout)
{
	zassert_equal(parent_log.ed_timeout_reqs, 1);
	zassert_true(parent_log.hdr.flags & ZZB_NWK_SECURITY);
	zassert_equal(parent_log.hdr.src, CHILD_ADDR);
	zassert_equal(parent_log.cmd.ed_timeout_req.timeout, CONFIG_ZZB_NWK_ED_TIMEOUT);
	zassert_equal(child->nwk.nib.parent_info, ZZB_NWK_PARENT_POLL_KEEPALIVE);
}

/* Three MAC data polls per timeout of 10 s (R22 3.6.10.3) */
ZTEST(zzb_nwk_join, test_keepalive_polls_the_parent)
{
	k_sleep(K_MSEC(3300));
	zassert_equal(parent_log.polls, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(parent_log.polls, 1);
	k_sleep(K_MSEC(3333));
	zassert_equal(parent_log.polls, 2);
	zassert_equal(nlme.status_count, 0);
}

static void rejoin(bool secure)
{
	zassert_ok(zzb_nlme_join_req(child, EXT_PAN_ID, ZZB_NWK_JOIN_REJOIN, BIT(CHANNEL), 0,
				     CAPABILITY, secure));
	k_sleep(K_MSEC(100));

	zassert_equal(nlme.join_count, 1);
	zassert_equal(nlme.join_status, ZZB_NWK_STATUS_SUCCESS);
	zassert_equal(parent_log.rejoin_reqs, 1);
	zassert_equal(child->nwk.nib.short_addr, CHILD_ADDR);
}

ZTEST(zzb_nwk_join, test_secure_rejoin)
{
	nlme.join_count = 0;
	rejoin(true);

	zassert_true(parent_log.rejoin_hdr.flags & ZZB_NWK_SECURITY);
	zassert_equal(parent_log.ed_timeout_reqs, 2);
	zassert_equal(zzb_nwk_neighbor_parent(child)->ext_addr, PARENT_EUI64);
}

/* The child knows the new parent from its beacon only, until the secured response */
ZTEST(zzb_nwk_join, test_secure_rejoin_to_a_new_parent)
{
	parent_start(ROUTER_ADDR, ROUTER_EUI64, CHILD_ADDR, ZZB_NWK_PARENT_POLL_KEEPALIVE);
	nlme.join_count = 0;
	rejoin(true);

	zassert_false(parent_log.rejoin_hdr.flags & ZZB_NWK_DST_IEEE);
	zassert_equal(zzb_nwk_neighbor_parent(child)->short_addr, ROUTER_ADDR);
	zassert_equal(zzb_nwk_neighbor_parent(child)->ext_addr, ROUTER_EUI64);
	zassert_equal(parent_log.ed_timeout_reqs, 1);
}

/* R22 4.6.3.3.2: without NWK security, then the key from the trust center */
ZTEST(zzb_nwk_join, test_trust_center_rejoin)
{
	nlme.join_count = 0;
	rejoin(false);

	zassert_false(parent_log.rejoin_hdr.flags & ZZB_NWK_SECURITY);
	zassert_true(child->nwk.key_pending);
	zassert_equal(parent_log.ed_timeout_reqs, 1);

	receive_network_key();
	zassert_equal(parent_log.ed_timeout_reqs, 2);
}

/* R22 3.6.10.4.1: a parent that has aged the child out asks it to rejoin */
ZTEST(zzb_nwk_join, test_leave_request_with_rejoin)
{
	parent_send_leave_request(true);
	k_sleep(K_MSEC(10));

	zassert_equal(parent_log.leaves, 1);
	zassert_equal(parent_log.cmd.leave_options, ZZB_NWK_LEAVE_REJOIN);
	zassert_equal(parent_log.hdr.dst, 0xfffd);
	zassert_equal(nlme.leave_ind_count, 1);
	zassert_true(nlme.leave_ind_rejoin);
	zassert_equal(child->nwk.nib.ext_pan_id, EXT_PAN_ID);
}

ZTEST(zzb_nwk_join, test_leave_without_rejoin)
{
	zassert_ok(zzb_nlme_leave_req(child, false));
	k_sleep(K_MSEC(10));

	zassert_equal(parent_log.leaves, 1);
	zassert_equal(parent_log.cmd.leave_options, 0);
	zassert_equal(nlme.leave_count, 1);
	zassert_equal(nlme.leave_status, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(child->nwk.nib.ext_pan_id, 0);
	zassert_equal(child->mac.pan_id, 0xffff);
	zassert_equal(child->mac.short_addr, 0xffff);
}

ZTEST_SUITE(zzb_nwk_join, NULL, setup, before, NULL, NULL);
