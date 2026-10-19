/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* bdbcMinCommissioningTime */
#define MIN_COMMISSIONING_TIME_S 180

/* The receiver stays on until milestone 18 */
static uint8_t capability(const struct zzb_node *node)
{
	uint8_t cap = ZZB_NWK_CAP_ALLOC_ADDR | ZZB_NWK_CAP_RX_ON_IDLE;

	if (node->device != NULL && (node->device->power_source & ZZB_POWER_MAINS)) {
		cap |= ZZB_NWK_CAP_MAINS;
	}

	return cap;
}

static void discover(struct zzb_node *node);

static void scan(struct zzb_node *node, bool secondary)
{
	node->bdb.state = ZZB_BDB_STEER_DISCOVERY;
	node->bdb.secondary = secondary;
	discover(node);
}

/* Step 13, then step 17 */
static void next_channels(struct zzb_node *node)
{
	if (node->bdb.secondary || CONFIG_ZZB_BDB_SECONDARY_CHANNELS == 0) {
		zzb_bdb_report(node, ZZB_BDB_EVENT_STEERING, ZZB_BDB_NO_NETWORK);
		return;
	}

	scan(node, true);
}

/* Step 3 on vScanChannels; step 2 passes an empty primary set to step 13 */
static void discover(struct zzb_node *node)
{
	uint32_t channels = node->bdb.secondary ? CONFIG_ZZB_BDB_SECONDARY_CHANNELS
						: CONFIG_ZZB_BDB_PRIMARY_CHANNELS;
	int err;

	if (channels == 0) {
		next_channels(node);
		return;
	}

	err = zzb_nlme_network_discovery_req(node, channels, ZZB_BDB_SCAN_DURATION);
	if (err == -EBUSY) {
		zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_RETRY_MS));
		return;
	}

	if (err) {
		next_channels(node);
	}
}

static bool failed(const struct zzb_bdb *bdb, uint64_t ext_pan_id)
{
	for (size_t i = 0; i < bdb->failed_count; i++) {
		if (bdb->failed[i] == ext_pan_id) {
			return true;
		}
	}

	return false;
}

/*
 * Steps 5 to 8: the next network with permit joining whose key has not failed. NWK tries every
 * parent of a network, so each network is tried once.
 */
static void join_next(struct zzb_node *node)
{
	struct zzb_bdb *bdb = &node->bdb;

	bdb->state = ZZB_BDB_STEER_JOIN;
	while (bdb->next_network < bdb->network_count) {
		const struct zzb_nwk_network *network = &bdb->networks[bdb->next_network];
		int err;

		if (!network->permit_join || failed(bdb, network->ext_pan_id)) {
			bdb->next_network++;
			continue;
		}

		err = zzb_nlme_join_req(node, network->ext_pan_id, ZZB_NWK_JOIN_ASSOC, 0, 0,
					capability(node), false);
		if (err == -EBUSY) {
			zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_RETRY_MS));
			return;
		}

		bdb->next_network++;
		if (err == 0) {
			bdb->ext_pan_id = network->ext_pan_id;
			return;
		}
	}

	next_channels(node);
}

/*
 * BDB 8.1 steps 3 and 5, 8.2 steps 14 and 16. Step 4 and 8.2 step 15 do not apply to an end
 * device, which has no permit joining of its own.
 */
void zzb_bdb_steering_done(struct zzb_node *node)
{
	zzb_zdo_mgmt_permit_joining_req(node, MIN_COMMISSIONING_TIME_S, true);
	zzb_bdb_report(node, ZZB_BDB_EVENT_STEERING, ZZB_BDB_SUCCESS);
}

int zzb_bdb_steer(struct zzb_node *node)
{
	if (node->bdb.state != ZZB_BDB_IDLE) {
		return -EBUSY;
	}

	if (node->bdb.on_network) {
		zzb_bdb_steering_done(node);
		return 0;
	}

	node->bdb.failed_count = 0;
	scan(node, false);
	return 0;
}

/*
 * Step 10. The association has removed the other networks from the neighbor table, and NLME-RESET
 * clears it, so the same channels are scanned again.
 */
static void key_timeout(struct zzb_node *node)
{
	struct zzb_bdb *bdb = &node->bdb;

	zzb_nlme_reset_req(node);
	zzb_aps_reset(node);
	if (bdb->failed_count == ARRAY_SIZE(bdb->failed)) {
		next_channels(node);
		return;
	}

	bdb->failed[bdb->failed_count++] = bdb->ext_pan_id;
	scan(node, bdb->secondary);
}

void zzb_bdb_steering_timer(struct zzb_node *node)
{
	switch (node->bdb.state) {
	case ZZB_BDB_STEER_DISCOVERY:
		discover(node);
		break;
	case ZZB_BDB_STEER_JOIN:
		join_next(node);
		break;
	case ZZB_BDB_STEER_KEY:
		key_timeout(node);
		break;
	default:
		break;
	}
}

void zzb_bdb_steering_discovery_conf(struct zzb_node *node, uint8_t status,
				     const struct zzb_nwk_network *networks, size_t count)
{
	struct zzb_bdb *bdb = &node->bdb;

	if (status != ZZB_MAC_STATUS_SUCCESS) {
		next_channels(node);
		return;
	}

	bdb->networks = networks;
	bdb->network_count = count;
	bdb->next_network = 0;
	join_next(node);
}

/* Step 9 */
void zzb_bdb_steering_join_conf(struct zzb_node *node, uint8_t status)
{
	if (status != ZZB_MAC_STATUS_SUCCESS) {
		join_next(node);
		return;
	}

	node->bdb.state = ZZB_BDB_STEER_KEY;
	zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_KEY_WAIT_MS));
}

/*
 * Step 11. apsTrustCenterAddress is known: APS drops a network key from 0xffffffffffffffff, and
 * ZDO sets the trust center address before the key.
 */
void zzb_bdb_steering_key_ind(struct zzb_node *node)
{
	node->bdb.on_network = true;
	zzb_zdo_device_annce_req(node);
	zzb_bdb_tclk_start(node);
}
