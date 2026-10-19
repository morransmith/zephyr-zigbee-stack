/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_APS_APS_H_
#define ZZB_SUBSYS_APS_APS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>

#include "aps/frame.h"
#include "common/fc.h"
#include "nwk/nwk.h"
#include "sec/aux_hdr.h"
#include "sec/mmo.h"

struct zzb_node;

/* Auxiliary header of an APS frame with the extended nonce */
#define ZZB_APS_AUX_MAX_LEN 13

static inline size_t zzb_aps_aux_len(bool ext_nonce)
{
	return ext_nonce ? ZZB_APS_AUX_MAX_LEN : ZZB_APS_AUX_MAX_LEN - 8;
}

/*
 * Headroom of APSDE-DATA.request: NWK headroom, APS header with a group address, auxiliary header
 * with the extended nonce. A frame needs the headroom of its own headers only.
 */
#define ZZB_APSDE_HEADROOM (ZZB_NLDE_HEADROOM + ZZB_APS_HDR_MAX_LEN + ZZB_APS_AUX_MAX_LEN)

/* DstAddrMode of APSDE-DATA.request (R22 Table 2-2) */
enum zzb_aps_addr_mode {
	ZZB_APS_ADDR_BINDING = 0x00,
	ZZB_APS_ADDR_GROUP = 0x01,
	ZZB_APS_ADDR_SHORT = 0x02,
	ZZB_APS_ADDR_EXT = 0x03,
};

/* TxOptions of APSDE-DATA.request */
#define ZZB_APS_TX_SECURITY  BIT(0)
#define ZZB_APS_TX_NWK_KEY   BIT(1)
#define ZZB_APS_TX_ACK       BIT(2)
#define ZZB_APS_TX_FRAGMENT  BIT(3)
#define ZZB_APS_TX_EXT_NONCE BIT(4)

/* APS sub-layer status values (R22 Table 2-27) */
enum zzb_aps_status {
	ZZB_APS_STATUS_SUCCESS = 0x00,
	ZZB_APS_STATUS_NO_ACK = 0xa7,
};

/* SecurityStatus of APSDE-DATA.indication; unsecured frames do not reach the upper layer */
enum zzb_aps_security_status {
	ZZB_APS_SECURED_NWK_KEY,
	ZZB_APS_SECURED_LINK_KEY,
};

struct zzb_apsde_req {
	enum zzb_aps_addr_mode dst_mode;
	/* Group or network address */
	uint16_t dst_addr;
	uint64_t dst_ext;
	uint8_t dst_ep;
	uint16_t profile;
	uint16_t cluster;
	uint8_t src_ep;
	uint8_t tx_options;
	uint8_t radius;
};

struct zzb_apsde_ind {
	/* The network destination of the frame */
	uint16_t dst_addr;
	uint8_t dst_ep;
	uint16_t src_addr;
	uint8_t src_ep;
	uint16_t profile;
	uint16_t cluster;
	enum zzb_aps_security_status security;
};

/* Entry of the transmission table; its index is the NSDU handle */
struct zzb_aps_tx {
	bool in_use;
	/* ASDU handle of the request */
	uint8_t handle;
};

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
	/* APS counter of the next frame */
	uint8_t counter;
	struct zzb_aps_tx tx[CONFIG_ZZB_APS_TX_COUNT];
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

/*
 * APSDE-DATA.request. buf holds the ASDU after the headroom of its headers, at most
 * ZZB_APSDE_HEADROOM, and its metadata the ASDU handle. Returns -ENOTSUP for binding, an
 * acknowledged transmission or fragmentation, -EINVAL for an acknowledged broadcast or group
 * frame, -ENOENT for an IEEE address without a network address or APS security without a
 * provisional or verified key, -ENOBUFS when the transmission table is full, -EMSGSIZE without
 * the headroom, or an error of NLDE-DATA.request.
 */
int zzb_apsde_data_req(struct zzb_node *node, struct net_buf *buf, const struct zzb_apsde_req *req);

/* Defined by ZDO. status is an APS status or the status of NLDE-DATA.confirm. */
void zzb_apsde_data_conf(struct zzb_node *node, uint8_t handle, uint8_t status);

/* Defined by ZDO. buf holds the ASDU, and its metadata the LQI. */
void zzb_apsde_data_ind(struct zzb_node *node, struct net_buf *buf,
			const struct zzb_apsde_ind *ind);

/* Within APS */

/* The key that removed the APS security of a frame */
struct zzb_aps_sec {
	enum zzb_sec_key_id key_id;
	uint64_t src_addr;
	/* NULL for the default trust center link key while the network key is pending */
	struct zzb_aps_key_pair *pair;
};

/*
 * Secures a frame in place (R22 4.4.1.1). buf holds the APS header of hdr_len bytes with
 * ZZB_APS_SECURITY set, then the payload, and has room for the auxiliary header before and the
 * MIC after. key_id ZZB_SEC_KEY_DATA uses link_key itself, ZZB_SEC_KEY_TRANSPORT and
 * ZZB_SEC_KEY_LOAD the keys derived from it (R22 4.5.3). Returns -EMSGSIZE without the room, or
 * an error of zzb_fc_next().
 */
int zzb_aps_secure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len,
		   const uint8_t link_key[ZZB_SEC_KEY_LEN], enum zzb_sec_key_id key_id,
		   bool ext_nonce);

/*
 * Verifies and decrypts a frame in place (R22 4.4.1.2), then removes the auxiliary header and the
 * MIC. nwk_src is the NWK source of the frame. Returns -EINVAL for a malformed auxiliary header or
 * the network key identifier, -ENOENT if no key is known for the sender, -EALREADY for a stale
 * frame counter, or -EBADMSG if the MIC does not match.
 */
int zzb_aps_unsecure(struct zzb_node *node, struct net_buf *buf, size_t hdr_len, uint16_t nwk_src,
		     struct zzb_aps_sec *sec);

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
