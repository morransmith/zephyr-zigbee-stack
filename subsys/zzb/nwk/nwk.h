/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_NWK_NWK_H_
#define ZZB_SUBSYS_NWK_NWK_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>

#include "sec/mmo.h"

struct zzb_node;

/* Auxiliary header of a NWK frame: security control, frame counter, source address, key sequence */
#define ZZB_NWK_AUX_LEN 14

/* Default radius: 2 * nwkMaxDepth (15 for the Zigbee PRO stack profile) */
#define ZZB_NWK_DEFAULT_RADIUS 30

/* Entry of nwkSecurityMaterialSet */
struct zzb_nwk_key {
	bool valid;
	uint8_t seq;
	uint8_t key[ZZB_SEC_KEY_LEN];
};

struct zzb_nib {
	uint16_t pan_id;
	uint16_t short_addr;
	uint64_t ext_pan_id;
	uint8_t seq;
	struct zzb_nwk_key keys[2];
	/* The slot of nwkActiveKeySeqNumber and the slot written last */
	uint8_t active_key;
	uint8_t newest_key;
};

enum zzb_nwk_relationship {
	ZZB_NWK_REL_PARENT = 0x00,
	ZZB_NWK_REL_CHILD = 0x01,
	ZZB_NWK_REL_SIBLING = 0x02,
	ZZB_NWK_REL_NONE = 0x03,
	ZZB_NWK_REL_PREVIOUS_CHILD = 0x04,
	ZZB_NWK_REL_UNAUTH_CHILD = 0x05,
};

struct zzb_nwk_neighbor {
	bool in_use;
	uint64_t ext_addr;
	uint16_t short_addr;
	enum zzb_nwk_relationship relationship;
	uint8_t lqi;
	/* Lowest accepted incoming frame counter per key slot (R22 4.3.1.2 step 6) */
	uint32_t in_fc[2];
};

/* Outgoing NWK frame counter, global to the node, and its saved watermark */
struct zzb_nwk_fc {
	uint32_t value;
	uint32_t watermark;
	bool saving;
	struct k_work save;
};

struct zzb_nwk {
	struct zzb_nib nib;
	struct zzb_nwk_fc fc;
	struct zzb_nwk_neighbor neighbors[CONFIG_ZZB_NEIGHBOR_COUNT];
};

/* Clears the NWK state. The NIB has no PAN ID, no network address and a random sequence number. */
void zzb_nwk_init(struct zzb_node *node);

/* NLME-SET of nwkPANId and nwkNetworkAddress, which also set macPANId and macShortAddress */
void zzb_nlme_set_pan_id(struct zzb_node *node, uint16_t pan_id);
void zzb_nlme_set_short_addr(struct zzb_node *node, uint16_t short_addr);

void zzb_nlme_set_ext_pan_id(struct zzb_node *node, uint64_t ext_pan_id);

/*
 * NLME-SET of a network key in nwkSecurityMaterialSet. The key replaces the key with the same
 * sequence number, otherwise the key that is not active, and the incoming frame counters of its
 * slot are reset. The first key becomes the active key.
 */
void zzb_nlme_set_nwk_key(struct zzb_node *node, const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t seq);

void zzb_nwk_fc_init(struct zzb_node *node);

/* Loads the watermark from the record fc/nwk */
int zzb_nwk_fc_load(struct zzb_node *node, size_t len, settings_read_cb read_cb, void *cb_arg);

/* Starts the counter at the loaded watermark and saves the next watermark */
int zzb_nwk_fc_start(struct zzb_node *node);

/*
 * Sets fc to the next outgoing frame counter. Returns -EBUSY while the counter is at a watermark
 * that is not saved yet, or -EOVERFLOW once the counter is 2^32-1.
 */
int zzb_nwk_fc_next(struct zzb_node *node, uint32_t *fc);

/*
 * Secures a frame in place (R22 4.3.1.1). buf holds the NWK header of hdr_len bytes with
 * ZZB_NWK_SECURITY set, then the payload, and has ZZB_NWK_AUX_LEN bytes of headroom and
 * ZZB_SEC_MIC_LEN bytes of tailroom. Returns -ENOENT without an active key, -EMSGSIZE without
 * the headroom or tailroom, or an error of zzb_nwk_fc_next().
 */
int zzb_nwk_secure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len);

/*
 * Verifies and decrypts a frame in place (R22 4.3.1.2), then removes the auxiliary header and the
 * MIC. Returns -EINVAL for a malformed auxiliary header or one without the network key identifier
 * and extended nonce, -EALREADY for a stale frame counter, -ENOENT if the key or the sender is not
 * known, or -EBADMSG if the MIC does not match.
 */
int zzb_nwk_unsecure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len);

/* Adds a neighbor, or returns NULL if the table is full. The caller checks for a duplicate. */
struct zzb_nwk_neighbor *zzb_nwk_neighbor_add(struct zzb_node *node, uint64_t ext_addr,
					      uint16_t short_addr,
					      enum zzb_nwk_relationship relationship);

void zzb_nwk_neighbor_remove(struct zzb_nwk_neighbor *neighbor);

struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_ext(struct zzb_node *node, uint64_t ext_addr);
struct zzb_nwk_neighbor *zzb_nwk_neighbor_find_short(struct zzb_node *node, uint16_t short_addr);
struct zzb_nwk_neighbor *zzb_nwk_neighbor_parent(struct zzb_node *node);

#endif /* ZZB_SUBSYS_NWK_NWK_H_ */
