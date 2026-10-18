/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zzb.h>

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
	};
	struct zzb_node *node;

	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	return zzb_node_init(&cfg, &node);
}
