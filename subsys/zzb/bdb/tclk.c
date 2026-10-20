/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "common/node.h"
#include "zdo/zdo.h"

/* bdbcTCLinkKeyExchangeTimeout */
#define TIMEOUT_MS 5000

/* bdbTCLinkKeyExchangeAttemptsMax */
#define ATTEMPTS_MAX 3

/* Bits 9 to 15 of the server mask (R22 Table 2-32) */
#define STACK_REVISION_SHIFT 9

/* Step 3. The confirm of the ZDO client times the attempt, the BDB timer a request not sent. */
static void node_desc(struct zzb_node *node)
{
	node->bdb.attempts++;
	if (zzb_zdo_node_desc_req(node, 0x0000) != 0) {
		zzb_bdb_schedule(node, K_MSEC(TIMEOUT_MS));
	}
}

/* Step 7; an error of the request counts as a failed attempt */
static void request_key(struct zzb_node *node)
{
	node->bdb.attempts++;
	zzb_apsme_request_key_req(node, zzb_apsme_get_tc_addr(node));
	zzb_bdb_schedule(node, K_MSEC(TIMEOUT_MS));
}

/* Step 11 */
static void verify_key(struct zzb_node *node)
{
	node->bdb.attempts++;
	zzb_apsme_verify_key_req(node, zzb_apsme_get_tc_addr(node), ZZB_APS_KEY_TC_LINK);
	zzb_bdb_schedule(node, K_MSEC(TIMEOUT_MS));
}

/* BDB 8.2 step 12 */
static void failed(struct zzb_node *node)
{
	zzb_bdb_leave_and_reset(node, ZZB_BDB_EVENT_STEERING, ZZB_BDB_TCLK_EX_FAILURE);
}

/* Steps 4a, 8a and 12a, or the failure of 4b, 8b and 12b */
static void retry(struct zzb_node *node, void (*attempt)(struct zzb_node *node))
{
	if (node->bdb.attempts == ATTEMPTS_MAX) {
		failed(node);
		return;
	}

	attempt(node);
}

static void start_step(struct zzb_node *node, enum zzb_bdb_state state,
		       void (*attempt)(struct zzb_node *node))
{
	zzb_bdb_set_state(node, state);
	node->bdb.attempts = 0;
	attempt(node);
}

/* Method 0, APS Request Key, is the only one */
void zzb_bdb_tclk_start(struct zzb_node *node)
{
	start_step(node, ZZB_BDB_TCLK_NODE_DESC, node_desc);
}

void zzb_bdb_tclk_timer(struct zzb_node *node)
{
	switch (node->bdb.state) {
	case ZZB_BDB_TCLK_NODE_DESC:
		retry(node, node_desc);
		break;
	case ZZB_BDB_TCLK_REQUEST_KEY:
		retry(node, request_key);
		break;
	case ZZB_BDB_TCLK_VERIFY_KEY:
		retry(node, verify_key);
		break;
	default:
		break;
	}
}

/* Steps 4 and 5; the trust center is at 0x0000 (R22 4.6.2.2) */
void zzb_zdo_node_desc_conf(struct zzb_node *node, uint8_t status, uint16_t addr,
			    uint16_t server_mask)
{
	if (node->bdb.state != ZZB_BDB_TCLK_NODE_DESC || addr != 0x0000) {
		return;
	}

	if (status != ZZB_ZDP_SUCCESS) {
		retry(node, node_desc);
		return;
	}

	/* A trust center of revision 20 or earlier does not exchange the key */
	if ((server_mask >> STACK_REVISION_SHIFT) < 21) {
		zzb_bdb_steering_done(node);
		return;
	}

	start_step(node, ZZB_BDB_TCLK_REQUEST_KEY, request_key);
}

/* Step 9; outside step 8 the key is unsolicited and ignored (BDB 10.2.6) */
void zzb_bdb_tc_link_key_ind(struct zzb_node *node, uint64_t src_addr, const uint8_t *key)
{
	uint64_t tc = zzb_apsme_get_tc_addr(node);
	const struct zzb_aps_key_pair *pair;

	if (node->bdb.state != ZZB_BDB_TCLK_REQUEST_KEY) {
		return;
	}

	pair = zzb_aps_key_pair_find(node, tc);
	if (pair == NULL || memcmp(pair->key, key, ZZB_SEC_KEY_LEN) == 0 ||
	    zzb_apsme_set_link_key(node, tc, key, ZZB_APS_KEY_UNVERIFIED,
				   ZZB_APS_LINK_KEY_UNIQUE) != 0) {
		failed(node);
		return;
	}

	start_step(node, ZZB_BDB_TCLK_VERIFY_KEY, verify_key);
}

/* Steps 12 and 13 */
void zzb_apsme_confirm_key_ind(struct zzb_node *node, uint8_t status, uint64_t src_addr,
			       enum zzb_aps_key_type type)
{
	if (node->bdb.state == ZZB_BDB_TCLK_VERIFY_KEY && status == ZZB_APS_STATUS_SUCCESS &&
	    type == ZZB_APS_KEY_TC_LINK) {
		zzb_bdb_steering_done(node);
	}
}
