/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/util.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "nwk/nwk.h"
#include "zdo/zdo.h"

/* The rejoin series (doc/architecture.md, section 13) */
static const struct {
	uint32_t channels;
	uint8_t duration;
	bool secure;
} steps[] = {
	/* BDB 7.1 step 4 */
	{0, 0, true},
	{ZZB_BDB_ALL_CHANNELS, ZZB_BDB_SCAN_DURATION, true},
	{ZZB_BDB_ALL_CHANNELS, ZZB_BDB_SCAN_DURATION, false},
};

int zzb_bdb_initialize(struct zzb_node *node)
{
	if (node->bdb.state != ZZB_BDB_IDLE) {
		return -EBUSY;
	}

	if (!node->bdb.on_network) {
		zzb_bdb_report(node, ZZB_BDB_EVENT_INIT, ZZB_BDB_NOT_ON_A_NETWORK);
		return 0;
	}

	zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_INIT);
	return 0;
}

/* The NWK callback that reports the cause may run inside a confirm, so the timer issues the step */
static void start_series(struct zzb_node *node)
{
	node->bdb.state = ZZB_BDB_REJOIN;
	node->bdb.step = 0;
	zzb_bdb_schedule(node, K_NO_WAIT);
}

void zzb_bdb_rejoin_start(struct zzb_node *node, enum zzb_bdb_event event)
{
	node->bdb.event = event;
	node->bdb.pause_ms = ZZB_BDB_PAUSE_MIN_MS;
	node->bdb.reported = false;
	start_series(node);
}

static void rejoined(struct zzb_node *node)
{
	zzb_zdo_device_annce_req(node);
	zzb_bdb_report(node, node->bdb.event, ZZB_BDB_SUCCESS);
}

/* The series has failed; only the first failure is reported */
static void series_failed(struct zzb_node *node)
{
	struct zzb_bdb *bdb = &node->bdb;
	bool report = !bdb->reported;

	bdb->state = ZZB_BDB_REJOIN_PAUSE;
	zzb_bdb_schedule(node, K_MSEC(bdb->pause_ms));
	bdb->pause_ms = MIN(2 * bdb->pause_ms, ZZB_BDB_PAUSE_MAX_MS);
	bdb->reported = true;
	if (report) {
		zzb_bdb_notify(node, bdb->event, ZZB_BDB_NO_NETWORK);
	}
}

static void next_step(struct zzb_node *node)
{
	if (++node->bdb.step == ARRAY_SIZE(steps)) {
		series_failed(node);
		return;
	}

	zzb_bdb_schedule(node, K_NO_WAIT);
}

static void request(struct zzb_node *node)
{
	int err = zzb_nlme_join_req(node, zzb_nlme_get_ext_pan_id(node), ZZB_NWK_JOIN_REJOIN,
				    steps[node->bdb.step].channels, steps[node->bdb.step].duration,
				    zzb_bdb_capability(node), steps[node->bdb.step].secure);

	if (err == -EBUSY) {
		zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_RETRY_MS));
		return;
	}

	if (err) {
		next_step(node);
	}
}

void zzb_bdb_rejoin_timer(struct zzb_node *node)
{
	switch (node->bdb.state) {
	case ZZB_BDB_REJOIN:
		request(node);
		break;
	case ZZB_BDB_REJOIN_KEY:
		series_failed(node);
		break;
	case ZZB_BDB_REJOIN_PAUSE:
		start_series(node);
		break;
	default:
		break;
	}
}

void zzb_bdb_rejoin_conf(struct zzb_node *node, uint8_t status)
{
	if (status != ZZB_NWK_STATUS_SUCCESS) {
		next_step(node);
		return;
	}

	if (steps[node->bdb.step].secure) {
		rejoined(node);
		return;
	}

	node->bdb.state = ZZB_BDB_REJOIN_KEY;
	zzb_bdb_schedule(node, K_MSEC(ZZB_BDB_KEY_WAIT_MS));
}

/*
 * During the pause NWK has a parent only after a trust center rejoin whose key did not arrive in
 * time; a late key ends the series
 */
void zzb_bdb_nwk_key_ind(struct zzb_node *node)
{
	switch (node->bdb.state) {
	case ZZB_BDB_REJOIN_KEY:
	case ZZB_BDB_REJOIN_PAUSE:
		rejoined(node);
		break;
	case ZZB_BDB_STEER_KEY:
		zzb_bdb_steering_key_ind(node);
		break;
	default:
		break;
	}
}

void zzb_nlme_nwk_status_ind(struct zzb_node *node, uint8_t code, uint16_t addr)
{
	bool own = addr == zzb_nlme_get_short_addr(node);
	bool rejoin = code == ZZB_NWK_CODE_PARENT_LINK_FAILURE ||
		      (code == ZZB_NWK_CODE_ADDR_CONFLICT && own);

	/* R22 3.6.1.9.2 */
	if (code == ZZB_NWK_CODE_ADDR_UPDATE && own) {
		zzb_zdo_device_annce_req(node);
		return;
	}

	if (rejoin && node->bdb.on_network && node->bdb.state == ZZB_BDB_IDLE) {
		zzb_bdb_rejoin_start(node, ZZB_BDB_EVENT_REJOIN);
	}
}
