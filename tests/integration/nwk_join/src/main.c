/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fake_radio.h"
#include "nwk/nwk.h"
#include "parent.h"

#define PARENT_ADDR  0x0000
#define PARENT_EUI64 0x00124b0000000001ULL
#define CHILD_ADDR   0x1234

/* Receiver on when idle, allocate address */
#define CAPABILITY (ZZB_NWK_CAP_RX_ON_IDLE | ZZB_NWK_CAP_ALLOC_ADDR)

static struct zzb_node *child;

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

/* NLDE callbacks, which APS defines from milestone 9 */
void zzb_nlde_data_ind(struct zzb_node *node, struct net_buf *buf, uint16_t dst_addr,
		       uint16_t src_addr, bool security_use)
{
	net_buf_unref(buf);
}

void zzb_nlde_data_conf(struct zzb_node *node, uint8_t handle, enum zzb_mac_status status)
{
}

/* Discovery on two channels, association, then the network key that APS would deliver */
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

	zzb_nlme_set_nwk_key(child, nwk_key, 0);
	k_sleep(K_MSEC(10));
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.radio = fake_radios[1],
	};

	fake_radio_reset();
	zassert_ok(zzb_node_init(&cfg, &child));
	return NULL;
}

static void before(void *fixture)
{
	memset(&nlme, 0, sizeof(nlme));
	parent_start(PARENT_ADDR, PARENT_EUI64, CHILD_ADDR, ZZB_NWK_PARENT_POLL_KEEPALIVE);
	zassert_ok(zzb_nlme_reset_req(child));
	join();
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

ZTEST_SUITE(zzb_nwk_join, NULL, setup, before, NULL, NULL);
