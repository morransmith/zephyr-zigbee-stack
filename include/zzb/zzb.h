/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZZB_H_
#define ZZB_INCLUDE_ZZB_ZZB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct zzb_node;

struct zzb_node_config {
	/** EUI-64 to use instead of the hardware one; 0 selects the hardware one. */
	uint64_t eui64;
	/**
	 * Radio device, e.g. DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)). Required with
	 * CONFIG_ZZB_MAC.
	 */
	const struct device *radio;
};

/**
 * @brief Allocate and initialize a node.
 *
 * @param cfg Node configuration, not referenced after the call returns.
 * @param node Set to the new node on success, NULL otherwise.
 *
 * @retval 0 on success.
 * @retval -EINVAL cfg->radio is NULL and CONFIG_ZZB_MAC is set.
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
