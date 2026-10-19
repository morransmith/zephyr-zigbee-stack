/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zzb.h>

/* Basic, Identify and On/Off servers of an On/Off Light (Home Automation profile) */
static const uint16_t light_in[] = {0x0000, 0x0003, 0x0006};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.in_clusters = light_in,
		.in_cluster_count = ARRAY_SIZE(light_in),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
	};
	struct zzb_node *node;

	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	return zzb_node_init(&cfg, &node);
}
