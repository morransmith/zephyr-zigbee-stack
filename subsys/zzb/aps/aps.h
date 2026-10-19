/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_APS_APS_H_
#define ZZB_SUBSYS_APS_APS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/settings/settings.h>

#include "aps/frame.h"
#include "common/fc.h"
#include "sec/mmo.h"

struct zzb_node;

/* apsTrustCenterAddress until the trust center is known (BDB 10.2.2) */
#define ZZB_APS_TC_UNKNOWN UINT64_MAX

/* KeyAttributes of a key-pair descriptor (R22 Table 4-29) */
enum zzb_aps_key_attr {
	ZZB_APS_KEY_PROVISIONAL = 0x00,
	ZZB_APS_KEY_UNVERIFIED = 0x01,
	ZZB_APS_KEY_VERIFIED = 0x02,
};

/* apsLinkKeyType */
enum zzb_aps_link_key_type {
	ZZB_APS_LINK_KEY_UNIQUE = 0x00,
	ZZB_APS_LINK_KEY_GLOBAL = 0x01,
};

/* Entry of apsDeviceKeyPairSet */
struct zzb_aps_key_pair {
	bool in_use;
	uint64_t addr;
	enum zzb_aps_key_attr attr;
	enum zzb_aps_link_key_type type;
	uint8_t key[ZZB_SEC_KEY_LEN];
	/* Lowest accepted incoming frame counter (R22 4.4.1.2 step 8) */
	uint32_t in_fc;
};

struct zzb_aib {
	uint64_t tc_addr;
	struct zzb_aps_key_pair keys[CONFIG_ZZB_APS_KEY_PAIR_COUNT];
};

struct zzb_aps {
	struct zzb_aib aib;
	/* Outgoing APS frame counter, kept by zzb_aps_reset() */
	struct zzb_fc fc;
};

/* The default global trust center link key "ZigBeeAlliance09" (BDB 6.3.1) */
extern const uint8_t zzb_aps_default_tc_link_key[ZZB_SEC_KEY_LEN];

/* Clears the APS state. The trust center is not known. */
void zzb_aps_init(struct zzb_node *node);

/*
 * Clears the AIB and the key-pair table and deletes their records. The outgoing frame counter
 * stays.
 */
void zzb_aps_reset(struct zzb_node *node);

/*
 * APSME-SET of apsTrustCenterAddress. When the key-pair table has no entry for addr, an entry
 * with the default trust center link key, global and provisional, is added. Returns -ENOMEM and
 * changes nothing when the table is full.
 */
int zzb_apsme_set_tc_addr(struct zzb_node *node, uint64_t addr);

/*
 * APSME-SET of the key-pair entry of addr: adds or replaces it with incoming frame counter 0.
 * Returns -ENOMEM when the table is full.
 */
int zzb_apsme_set_link_key(struct zzb_node *node, uint64_t addr, const uint8_t key[ZZB_SEC_KEY_LEN],
			   enum zzb_aps_key_attr attr, enum zzb_aps_link_key_type type);

struct zzb_aps_key_pair *zzb_aps_key_pair_find(struct zzb_node *node, uint64_t addr);

/* Within APS */

/* Deletes the records aps/tc and aps/key/<i> */
void zzb_aps_delete(struct zzb_node *node);

/*
 * Load the records aps/tc and aps/key/<index>; -EINVAL for an unknown version or length. A key
 * record with an index outside the table is ignored.
 */
int zzb_aps_tc_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg);
int zzb_aps_key_load(struct zzb_node *node, int index, size_t len, settings_read_cb read_cb,
		     void *cb_arg);

#endif /* ZZB_SUBSYS_APS_APS_H_ */
