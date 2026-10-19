/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zzb.h>

/* Basic, Identify and On/Off servers of an On/Off Light (Home Automation profile) */
static const struct zzb_zcl_cluster light[] = {
	{.id = 0x0000, .server = true},
	{.id = 0x0003, .server = true},
	{.id = 0x0006, .server = true},
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

static void bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
}

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = bdb_event,
	};
	struct zzb_node *node;
	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	int err = zzb_node_init(&cfg, &node);

	if (err || !IS_ENABLED(CONFIG_ZZB_BDB)) {
		return err;
	}

	return zzb_bdb_start(node);
}
