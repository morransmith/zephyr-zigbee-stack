/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/ota.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

#define LIGHT_EP 1

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "ota",
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
	.sw_build_id = APP_VERSION_STRING,
};

static struct zzb_zcl_identify identify;
static struct zzb_zcl_scenes scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

static struct zzb_zcl_ota ota = {
	.current_file_version = APPVERSION,
	.image_type = CONFIG_SAMPLE_OTA_IMAGE_TYPE,
};

static const struct zzb_zcl_cluster light[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_OTA_CLIENT(&ota),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = LIGHT_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_ON_OFF_LIGHT,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_ON_OFF_LIGHT),
		.clusters = light,
		.cluster_count = ARRAY_SIZE(light),
	},
};

static const struct zzb_device device = {
	.manufacturer_code = CONFIG_SAMPLE_MANUFACTURER_CODE,
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* OnOff for the button, which runs outside the stack thread */
static atomic_t light_on;

static void light_set(bool on)
{
	atomic_set(&light_on, on);
	sample_led(on);
	LOG_INF("light %s", on ? "on" : "off");
}

/* In the swap modes MCUboot reverts an image that is not confirmed at the next reset */
static void bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	sample_bdb_event(node, event, status);
	if (event != ZZB_BDB_EVENT_INIT || status != ZZB_BDB_SUCCESS || boot_is_img_confirmed()) {
		return;
	}

	LOG_INF("confirm image %d", boot_write_img_confirmed());
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			light_set(on_off.on_off);
		}
		break;
	case ZZB_ZCL_OTA:
		if (attr == ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS) {
			LOG_INF("ImageUpgradeStatus %u", ota.image_upgrade_status);
		} else if (attr == ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION) {
			LOG_INF("DownloadedFileVersion 0x%08x", ota.downloaded_file_version);
		}
		break;
	default:
		break;
	}
}

/* MCUboot installs the image at the next boot */
static void ota_upgrade(struct zzb_node *node)
{
	LOG_INF("reboot");
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

void sample_button(struct zzb_node *node)
{
	bool on = !atomic_get(&light_on);
	int err = zzb_zcl_attr_set(node, LIGHT_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	light_set(on);
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, LIGHT_EP));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = bdb_event,
		.zcl_cb = zcl_changed,
		.ota_cb = ota_upgrade,
	};
	struct zzb_node *node;
	int err;

	LOG_INF("version %s (0x%08x), image confirmed %d", APP_VERSION_STRING, APPVERSION,
		boot_is_img_confirmed());
	err = sample_init(&cfg, &node);
	if (err) {
		return err;
	}

	/* StartUpOnOff has set OnOff without zcl_cb */
	light_set(on_off.on_off);
	return zzb_bdb_start(node);
}
