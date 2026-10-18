/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "common/buf.h"
#include "common/node.h"
#include "fakes.h"
#include "mac/frame.h"
#include "nwk/frame.h"
#include "nwk/nwk.h"

#define EPID_A 0x00124b0000001a62ULL
#define EPID_B 0x00124b0000002b73ULL
#define PAN_A  0x1a62
#define PAN_B  0x2b73

/* Superframe Specification fields with association permit, and without */
#define SF_COORD  (0x0fff | ZZB_MAC_SF_PAN_COORD | ZZB_MAC_SF_ASSOC_PERMIT)
#define SF_ROUTER (0x0fff | ZZB_MAC_SF_ASSOC_PERMIT)
#define SF_CLOSED 0x0fff

static const struct zzb_nwk_beacon coord_a = {
	.stack_profile = 2,
	.protocol_version = 2,
	.router_capacity = true,
	.ext_pan_id = EPID_A,
	.tx_offset = 0xffffff,
	.update_id = 3,
};

static const struct zzb_nwk_beacon router_a = {
	.stack_profile = 2,
	.protocol_version = 2,
	.depth = 1,
	.ed_capacity = true,
	.ext_pan_id = EPID_A,
	.tx_offset = 0xffffff,
	.update_id = 3,
};

static const struct zzb_nwk_beacon coord_b = {
	.stack_profile = 2,
	.protocol_version = 2,
	.ed_capacity = true,
	.ext_pan_id = EPID_B,
	.tx_offset = 0xffffff,
};

static struct zzb_node *node;

/* The discovery entry of the router with short_addr in the network ext_pan_id */
static struct zzb_nwk_neighbor *router(uint64_t ext_pan_id, uint16_t short_addr)
{
	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		if (nb->in_use && nb->beacon.ext_pan_id == ext_pan_id &&
		    nb->short_addr == short_addr) {
			return nb;
		}
	}

	return NULL;
}

static int entry_count(void)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(node->nwk.neighbors, nb) {
		count += nb->in_use;
	}

	return count;
}

static void discover(void)
{
	zassert_ok(zzb_nlme_network_discovery_req(node, BIT(15) | BIT(20), 3));
}

static void end_discovery(size_t count)
{
	zzb_mlme_scan_conf(node, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_nlme_network_discovery_conf_fake.call_count, 1);
	zassert_equal_ptr(zzb_nlme_network_discovery_conf_fake.arg0_val, node);
	zassert_equal(zzb_nlme_network_discovery_conf_fake.arg1_val, ZZB_MAC_STATUS_SUCCESS);
	zassert_equal(zzb_nlme_network_discovery_conf_fake.arg3_val, count);
}

static void *setup(void)
{
	node = test_node(0);
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	zzb_nwk_init(node);
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

ZTEST(zzb_nwk_join, test_discovery_reports_networks)
{
	const struct zzb_nwk_network *networks;
	struct zzb_nwk_neighbor *nb;

	discover();
	zassert_equal(zzb_mlme_scan_req_fake.call_count, 1);
	zassert_equal(zzb_mlme_scan_req_fake.arg1_val, BIT(15) | BIT(20));
	zassert_equal(zzb_mlme_scan_req_fake.arg2_val, 3);

	notify_beacon(node, &coord_a, PAN_A, 0x0000, 15, SF_COORD, 120);
	notify_beacon(node, &router_a, PAN_A, 0x1111, 15, SF_CLOSED, 200);
	notify_beacon(node, &coord_b, PAN_B, 0x0000, 20, SF_COORD, 90);
	end_discovery(2);

	/* Permit joining and the capacities are ORed over the routers */
	networks = zzb_nlme_network_discovery_conf_fake.arg2_val;
	zassert_equal(networks[0].ext_pan_id, EPID_A);
	zassert_equal(networks[0].pan_id, PAN_A);
	zassert_equal(networks[0].channel, 15);
	zassert_equal(networks[0].update_id, 3);
	zassert_true(networks[0].permit_join);
	zassert_true(networks[0].router_capacity);
	zassert_true(networks[0].ed_capacity);
	zassert_equal(networks[1].ext_pan_id, EPID_B);
	zassert_equal(networks[1].pan_id, PAN_B);
	zassert_equal(networks[1].channel, 20);
	zassert_false(networks[1].router_capacity);

	nb = router(EPID_A, 0x0000);
	zassert_not_null(nb);
	zassert_equal(nb->ext_addr, 0);
	zassert_equal(nb->relationship, ZZB_NWK_REL_NONE);
	zassert_equal(nb->device_type, ZZB_NWK_DEVICE_COORDINATOR);
	zassert_equal(nb->lqi, 120);
	zassert_equal(nb->pan_id, PAN_A);
	zassert_equal(nb->channel, 15);
	zassert_true(nb->permit_join);
	zassert_true(nb->potential_parent);
	zassert_true(nb->beacon.router_capacity);

	/* Without association permit a router is not a candidate */
	zassert_is_null(router(EPID_A, 0x1111));
	zassert_not_null(router(EPID_B, 0x0000));
}

ZTEST(zzb_nwk_join, test_repeated_beacon_updates_its_entry)
{
	discover();
	notify_beacon(node, &router_a, PAN_A, 0x1111, 15, SF_ROUTER, 100);
	notify_beacon(node, &router_a, PAN_A, 0x1111, 15, SF_ROUTER, 150);
	end_discovery(1);

	zassert_equal(entry_count(), 1);
	zassert_equal(router(EPID_A, 0x1111)->lqi, 150);
	zassert_equal(router(EPID_A, 0x1111)->device_type, ZZB_NWK_DEVICE_ROUTER);
}

/* R22 3.6.1.4.1: every scan makes the remaining entries potential parents again */
ZTEST(zzb_nwk_join, test_scan_removes_previous_results)
{
	struct zzb_nwk_neighbor *parent =
		zzb_nwk_neighbor_add(node, 0x0102030405060708ULL, 0x2222, ZZB_NWK_REL_PARENT);

	discover();
	notify_beacon(node, &router_a, PAN_A, 0x1111, 15, SF_ROUTER, 100);
	end_discovery(1);

	RESET_FAKE(zzb_nlme_network_discovery_conf);
	discover();
	end_discovery(0);

	zassert_is_null(router(EPID_A, 0x1111));
	zassert_true(parent->in_use);
	zassert_true(parent->potential_parent);
}

ZTEST(zzb_nwk_join, test_unusable_beacons_are_ignored)
{
	struct zzb_nwk_beacon other = coord_a;
	struct net_buf *buf;

	/* Outside a discovery */
	notify_beacon(node, &coord_a, PAN_A, 0x0000, 15, SF_COORD, 100);

	discover();
	other.stack_profile = 1;
	notify_beacon(node, &other, PAN_A, 0x0000, 15, SF_COORD, 100);
	other.stack_profile = 2;
	other.protocol_version = 1;
	notify_beacon(node, &other, PAN_A, 0x0000, 15, SF_COORD, 100);

	/* Extended source address, then a truncated payload */
	buf = zzb_buf_alloc(K_NO_WAIT);
	zassert_not_null(buf);
	zzb_nwk_beacon_write(&coord_a, net_buf_add(buf, ZZB_NWK_BEACON_LEN));
	zzb_frame_meta(buf)->src = (struct zzb_mac_addr){
		.mode = ZZB_MAC_ADDR_EXT,
		.pan_id = PAN_A,
		.ext_addr = 0x0102030405060708ULL,
	};
	zzb_mlme_beacon_notify_ind(node, buf, 15, SF_COORD);

	buf = zzb_buf_alloc(K_NO_WAIT);
	zassert_not_null(buf);
	net_buf_add(buf, ZZB_NWK_BEACON_LEN - 1);
	zzb_mlme_beacon_notify_ind(node, buf, 15, SF_COORD);

	end_discovery(0);
	zassert_equal(entry_count(), 0);
}

ZTEST(zzb_nwk_join, test_full_tables_keep_descriptors)
{
	struct zzb_nwk_beacon other = coord_b;

	discover();
	for (int i = 0; i <= CONFIG_ZZB_NEIGHBOR_COUNT; i++) {
		notify_beacon(node, &router_a, PAN_A, 0x1000 + i, 15, SF_ROUTER, 100);
	}

	/* A full neighbor table keeps the descriptor of a new network */
	notify_beacon(node, &coord_b, PAN_B, 0x0000, 20, SF_COORD, 100);
	for (int i = 0; i < CONFIG_ZZB_NWK_NETWORK_COUNT; i++) {
		other.ext_pan_id = EPID_B + 1 + i;
		notify_beacon(node, &other, PAN_B + 1 + i, 0x0000, 20, SF_COORD, 100);
	}

	end_discovery(CONFIG_ZZB_NWK_NETWORK_COUNT);
	zassert_equal(entry_count(), CONFIG_ZZB_NEIGHBOR_COUNT);
	zassert_equal(zzb_nlme_network_discovery_conf_fake.arg2_val[1].ext_pan_id, EPID_B);
}

ZTEST(zzb_nwk_join, test_requests_during_discovery_are_busy)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_reserve(buf, ZZB_NLDE_HEADROOM);

	discover();
	zassert_equal(zzb_nlme_network_discovery_req(node, BIT(15), 3), -EBUSY);
	zassert_equal(zzb_nlde_data_req(node, buf, 0x0000, 0, false), -EBUSY);
	end_discovery(0);

	/* A scan confirm outside a discovery is ignored */
	zzb_mlme_scan_conf(node, ZZB_MAC_STATUS_NO_BEACON);
	zassert_equal(zzb_nlme_network_discovery_conf_fake.call_count, 1);

	zassert_equal(zzb_nlde_data_req(node, buf, 0x0000, 0, false), -ENOTCONN);
	net_buf_unref(buf);
}

ZTEST(zzb_nwk_join, test_scan_error_is_returned)
{
	zzb_mlme_scan_req_fake.return_val = -EINVAL;

	zassert_equal(zzb_nlme_network_discovery_req(node, 0, 3), -EINVAL);
	zzb_mlme_scan_req_fake.return_val = 0;
	discover();
	end_discovery(0);
}

ZTEST_SUITE(zzb_nwk_join, NULL, setup, before, after, NULL);
