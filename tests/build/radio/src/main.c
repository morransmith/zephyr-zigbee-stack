/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

/* Basic, Identify, Groups, Scenes and On/Off servers of a Home Automation On/Off Light */
#if defined(CONFIG_ZZB_ZCL)
static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "build",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

static struct zzb_zcl_identify identify;

static struct zzb_zcl_scenes scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
};
#else
/* native_sim builds no ZCL, so the definitions of the stack clusters do not exist */
static const struct zzb_zcl_cluster light[] = {
	{.id = ZZB_ZCL_BASIC, .server = true},
	{.id = ZZB_ZCL_IDENTIFY, .server = true},
	{.id = ZZB_ZCL_GROUPS, .server = true},
	{.id = ZZB_ZCL_SCENES, .server = true},
	{.id = ZZB_ZCL_ON_OFF, .server = true},
};
#endif

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

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
}

static void zcl_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
		       uint8_t variant)
{
}

int main(void)
{
	/* native_sim has no radio and builds no MAC */
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_effect_cb = zcl_effect,
	};
	struct zzb_node *node;
	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	int err = zzb_node_init(&cfg, &node);

	if (err || !IS_ENABLED(CONFIG_ZZB_BDB)) {
		return err;
	}

	return zzb_bdb_start(node);
}
