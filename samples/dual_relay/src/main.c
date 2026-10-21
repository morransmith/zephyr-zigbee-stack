/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(dual_relay, LOG_LEVEL_INF);

/* Relay i is on endpoint i + 1 */
#define RELAY_COUNT 2
#define SWITCH_EP   3

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = "dual_relay",
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify relay1_identify;
static struct zzb_zcl_identify relay2_identify;
static struct zzb_zcl_identify switch_identify;
static struct zzb_zcl_scenes relay1_scenes;
static struct zzb_zcl_scenes relay2_scenes;

static struct zzb_zcl_on_off relays[RELAY_COUNT] = {
	{.start_up_default = ZZB_ZCL_START_UP_PREVIOUS},
	{.start_up_default = ZZB_ZCL_START_UP_PREVIOUS},
};

static const struct zzb_zcl_cluster relay1_clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&relay1_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&relay1_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&relays[0]),
};

static const struct zzb_zcl_cluster relay2_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&relay2_identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&relay2_scenes),
	ZZB_ZCL_ON_OFF_SERVER(&relays[1]),
};

static const struct zzb_zcl_cluster switch_clusters[] = {
	ZZB_ZCL_IDENTIFY_SERVER(&switch_identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
};

/* Two On/Off Lights and an On/Off Light Switch */
static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_ON_OFF_LIGHT,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_ON_OFF_LIGHT),
		.clusters = relay1_clusters,
		.cluster_count = ARRAY_SIZE(relay1_clusters),
	},
	{
		.id = 2,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_ON_OFF_LIGHT,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_ON_OFF_LIGHT),
		.clusters = relay2_clusters,
		.cluster_count = ARRAY_SIZE(relay2_clusters),
	},
	{
		.id = SWITCH_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = ZZB_DEVICE_ON_OFF_LIGHT_SWITCH,
		.device_version = ZZB_DEVICE_VERSION(ZZB_DEVICE_ON_OFF_LIGHT_SWITCH),
		.clusters = switch_clusters,
		.cluster_count = ARRAY_SIZE(switch_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* led0 is relay 1, led1 relay 2 */
static const struct gpio_dt_spec relay2_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});

/* OnOff of the relays for the button, which runs outside the stack thread */
static atomic_t relay_on[RELAY_COUNT];

static void relay_set(int i, bool on)
{
	atomic_set(&relay_on[i], on);
	if (i == 0) {
		sample_led(on);
	} else if (gpio_is_ready_dt(&relay2_led)) {
		gpio_pin_set_dt(&relay2_led, on);
	}

	LOG_INF("relay %d %s", i + 1, on ? "on" : "off");
}

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(relay1_identify.identify_time > 0 ||
				relay2_identify.identify_time > 0 ||
				switch_identify.identify_time > 0);
		return;
	}

	if (cluster == ZZB_ZCL_ON_OFF && attr == ZZB_ZCL_ON_OFF_ON_OFF && ep <= RELAY_COUNT) {
		relay_set(ep - 1, relays[ep - 1].on_off);
	}
}

static void relay_toggle(struct zzb_node *node, int i)
{
	bool on = !atomic_get(&relay_on[i]);
	int err = zzb_zcl_attr_set(node, i + 1, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff of relay %d: %d", i + 1, err);
		return;
	}

	relay_set(i, on);
}

/* Relay 1, relay 2 and the bound lights in turn */
void sample_button(struct zzb_node *node)
{
	static int press;
	int i = press++ % (RELAY_COUNT + 1);

	if (i < RELAY_COUNT) {
		relay_toggle(node, i);
		return;
	}

	LOG_INF("toggle bound lights %d", zzb_zcl_send_bound(node, SWITCH_EP, ZZB_ZCL_ON_OFF,
							     ZZB_ZCL_ON_OFF_TOGGLE, NULL, 0));
}

void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, SWITCH_EP, ZZB_BDB_FB_UNICAST));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
	};
	struct zzb_node *node;
	int err;

	if (gpio_is_ready_dt(&relay2_led)) {
		gpio_pin_configure_dt(&relay2_led, GPIO_OUTPUT_INACTIVE);
	}

	err = sample_init(&cfg, &node);
	if (err) {
		return err;
	}

	/* StartUpOnOff has set OnOff without zcl_cb */
	for (int i = 0; i < RELAY_COUNT; i++) {
		relay_set(i, relays[i].on_off);
	}

	return zzb_bdb_start(node);
}
