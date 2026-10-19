/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_NODE_H_
#define ZZB_SUBSYS_COMMON_NODE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zzb.h>

#include "aps/aps.h"
#include "bdb/bdb.h"
#include "mac/mac.h"
#include "nwk/nwk.h"
#include "radio/port.h"
#include "zcl/zcl.h"
#include "zdo/zdo.h"

struct zzb_node {
	bool in_use;
	uint64_t eui64;
	const struct zzb_device *device;
	zzb_bdb_cb_t bdb_cb;
	zzb_zcl_cb_t zcl_cb;
	zzb_zcl_effect_cb_t zcl_effect_cb;
	struct zzb_radio_port port;
	struct zzb_mac mac;
	struct zzb_nwk nwk;
	struct zzb_aps aps;
	struct zzb_zdo zdo;
	struct zzb_bdb bdb;
	struct zzb_zcl zcl;
};

/* Index of the node in the pool */
int zzb_node_index(const struct zzb_node *node);

/* The endpoint id of the device definition, or NULL */
const struct zzb_endpoint *zzb_node_endpoint(const struct zzb_node *node, uint8_t id);

#endif /* ZZB_SUBSYS_COMMON_NODE_H_ */
