/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZZB_H_
#define ZZB_INCLUDE_ZZB_ZZB_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util_macro.h>
#include <zzb/bdb.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct zzb_node;

/** An endpoint with the fields of its simple descriptor (R22 2.3.2.5). */
struct zzb_endpoint {
	/** Endpoint ID, 1 to 254. */
	uint8_t id;
	uint16_t profile;
	uint16_t device_id;
	uint8_t device_version;
	const struct zzb_zcl_cluster *clusters;
	uint8_t cluster_count;
};

/** Power sources of the power descriptor (R22 Table 2-36). */
enum zzb_power_source {
	ZZB_POWER_MAINS = BIT(0),
	ZZB_POWER_RECHARGEABLE = BIT(1),
	ZZB_POWER_DISPOSABLE = BIT(2),
};

/**
 * Device definition. Every simple descriptor must fit in apscMaxDescriptorSize (64 bytes, 28
 * clusters); the stack does not check it.
 */
struct zzb_device {
	uint16_t manufacturer_code;
	enum zzb_power_source power_source;
	/** Endpoints in ascending order of ID. */
	const struct zzb_endpoint *endpoints;
	size_t endpoint_count;
};

struct zzb_node_config {
	/** EUI-64 to use instead of the hardware one; 0 selects the hardware one. */
	uint64_t eui64;
	/**
	 * Radio device, e.g. DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)). Required with
	 * CONFIG_ZZB_MAC.
	 */
	const struct device *radio;
	/** Device definition, kept by the node. Required with CONFIG_ZZB_ZDO. */
	const struct zzb_device *device;
	/** BDB event callback, or NULL. */
	zzb_bdb_cb_t bdb_cb;
	/** Callback for attribute values changed by the stack, or NULL. */
	zzb_zcl_cb_t zcl_cb;
	/** Callback for the effects of ZCL commands, or NULL. */
	zzb_zcl_effect_cb_t zcl_effect_cb;
	/**
	 * Install code with its CRC (BDB 10.1.1), 8, 10, 14 or 18 bytes, or NULL. The node joins
	 * with the key derived from it or with the default trust center link key.
	 */
	const uint8_t *install_code;
	size_t install_code_len;
};

/**
 * @brief Allocate and initialize a node.
 *
 * @param cfg Node configuration. The node keeps cfg->device, which must outlive it; the rest is
 *            not referenced after the call returns.
 * @param node Set to the new node on success, NULL otherwise.
 *
 * With CONFIG_ZZB_NWK the node loads its settings records and saves the next outgoing frame
 * counter watermark; a settings error is returned as it is.
 *
 * @retval 0 on success.
 * @retval -EINVAL cfg->radio is NULL and CONFIG_ZZB_MAC is set, cfg->device is NULL and
 *                 CONFIG_ZZB_ZDO is set, the install code has another length or a wrong CRC, or a
 *                 settings record of the node has an unknown version or length.
 * @retval -ENOMEM all CONFIG_ZZB_NODE_COUNT nodes are in use.
 * @retval -ENODEV cfg->eui64 is 0 and the hardware provides no EUI-64, or the custom-L2 backend
 *                 finds no interface for cfg->radio.
 * @retval -ENOTSUP the radio driver does not wait for the ACK in tx().
 * @retval -EIO the radio does not start.
 */
int zzb_node_init(const struct zzb_node_config *cfg, struct zzb_node **node);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZZB_H_ */
