/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_INTEGRATION_NWK_JOIN_PARENT_H_
#define ZZB_TESTS_INTEGRATION_NWK_JOIN_PARENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "nwk/frame.h"
#include "sec/mmo.h"

#define PAN_ID     0x1a62
#define EXT_PAN_ID 0x00124b0000001a62ULL
#define CHANNEL    15

/* The test network key, also in ci/check-frame-log.sh */
extern const uint8_t nwk_key[ZZB_SEC_KEY_LEN];

/* What the parent has received from the child */
struct parent_log {
	int beacon_reqs;
	int polls;
	int ed_timeout_reqs;
	int rejoin_reqs;
	int leaves;
	/* The last NWK command */
	struct zzb_nwk_hdr hdr;
	struct zzb_nwk_cmd cmd;
};

extern struct parent_log parent_log;

/*
 * Plays a parent on fake radio 0 with short_addr and eui64. It answers beacon requests,
 * associations with child_addr, End Device Timeout Requests with parent_info, and rejoin
 * requests, and clears parent_log.
 */
void parent_start(uint16_t short_addr, uint64_t eui64, uint16_t child_addr, uint8_t parent_info);

/* Sends a leave request to the child, from the stack thread */
void parent_send_leave_request(bool rejoin);

#endif /* ZZB_TESTS_INTEGRATION_NWK_JOIN_PARENT_H_ */
