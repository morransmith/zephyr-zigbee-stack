/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/ias_ace.h>
#include <zzb/zcl/ias_wd.h>
#include <zzb/zcl/ias_zone.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(ias, LOG_LEVEL_INF);

#define IAS_EP 1

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
};

#if defined(CONFIG_SAMPLE_SLEEPY)
/* Two AA cells at 3.0 V and 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = 0x03,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
#endif

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_ZONE)
static struct zzb_zcl_ias_zone zone = {
	.zone_type = CONFIG_SAMPLE_ZONE_TYPE,
};
#endif

#if defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
static struct zzb_zcl_ias_wd wd;
#endif

static const struct zzb_zcl_cluster ias[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
#if defined(CONFIG_SAMPLE_SLEEPY)
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
#endif
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
#if defined(CONFIG_SAMPLE_ZONE)
	ZZB_ZCL_IAS_ZONE_SERVER(&zone),
#endif
#if defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_IAS_WD_SERVER(&wd),
#endif
#if defined(CONFIG_SAMPLE_IAS_ACE)
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_IAS_ACE_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_IAS_CIE)
	ZZB_ZCL_IAS_ACE_SERVER(),
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_IAS_ZONE_CLIENT(),
	ZZB_ZCL_IAS_WD_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = IAS_EP,
		.profile = 0x0104,
		.device_id = CONFIG_SAMPLE_DEVICE_ID,
		.device_version = CONFIG_SAMPLE_DEVICE_VERSION,
		.clusters = ias,
		.cluster_count = ARRAY_SIZE(ias),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* A warning of the IAS WD server runs; used on the stack thread */
static bool warning;

#if defined(CONFIG_SAMPLE_ZONE)
/* ZoneID, which the keypad sends in Arm */
static atomic_t zone_id;
#endif

static void blink_update(void)
{
	sample_identify(identify.identify_time > 0 || warning);
}

#if defined(CONFIG_SAMPLE_IAS_ZONE) || defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
/* Alarm1 of ZoneStatus for the button */
static atomic_t alarm;

static void zone_alarm(struct zzb_node *node)
{
	bool on = !atomic_get(&alarm);
	uint16_t status = on ? ZZB_ZCL_IAS_ZONE_STATUS_ALARM1 : 0;
	int err =
		zzb_zcl_attr_set(node, IAS_EP, ZZB_ZCL_IAS_ZONE, ZZB_ZCL_IAS_ZONE_STATUS, &status);

	if (err) {
		LOG_ERR("ZoneStatus %d", err);
		return;
	}

	atomic_set(&alarm, on);
	sample_led(on);
	LOG_INF("alarm %s", on ? "on" : "off");
}
#endif

#if defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
/* Stop is warning mode 0 without strobe; another command with a duration of 0 changes nothing */
static uint8_t start_warning(const struct zzb_zcl_cmd *cmd)
{
	uint8_t mode = FIELD_GET(ZZB_ZCL_IAS_WD_WARNING_MODE, cmd->payload[0]);
	bool strobe = FIELD_GET(ZZB_ZCL_IAS_WD_STROBE, cmd->payload[0]);
	uint16_t duration = sys_get_le16(&cmd->payload[1]);

	LOG_INF("warning mode %u strobe %u for %u s", mode, strobe, duration);
	if (mode == 0 && !strobe) {
		warning = false;
	} else if (duration > 0) {
		warning = true;
	}

	blink_update();
	return ZZB_ZCL_SUCCESS;
}

static uint8_t wd_cmd(const struct zzb_zcl_cmd *cmd)
{
	if (cmd->id == ZZB_ZCL_IAS_WD_START_WARNING) {
		return start_warning(cmd);
	}

	LOG_INF("squawk 0x%02x", cmd->payload[0]);
	return ZZB_ZCL_SUCCESS;
}
#endif

#if defined(CONFIG_SAMPLE_IAS_ACE)
/* Arm with Arm All Zones and with Disarm in turn */
static void keypad_arm(struct zzb_node *node)
{
	static bool disarm;
	/* Arm Mode, Arm/Disarm Code "1234" and Zone ID (ZCL Figure 8-6) */
	const uint8_t arm[] = {
		disarm ? ZZB_ZCL_IAS_ACE_MODE_DISARM : ZZB_ZCL_IAS_ACE_MODE_ALL_ZONES,
		4,
		'1',
		'2',
		'3',
		'4',
		atomic_get(&zone_id),
	};
	int err = zzb_zcl_send_bound(node, IAS_EP, ZZB_ZCL_IAS_ACE, ZZB_ZCL_IAS_ACE_ARM, arm,
				     sizeof(arm));

	if (err) {
		LOG_ERR("Arm %d", err);
		return;
	}

	LOG_INF("%s sent", disarm ? "disarm" : "arm");
	disarm = !disarm;
}

/* Arm Notification and Panel Status are 0 for disarmed and 1 to 3 for armed */
static uint8_t keypad_cmd(const struct zzb_zcl_cmd *cmd)
{
	if (cmd->id != ZZB_ZCL_IAS_ACE_ARM_RSP && cmd->id != ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED) {
		return ZZB_ZCL_SUCCESS;
	}

	LOG_INF("panel 0x%02x", cmd->payload[0]);
	if (cmd->payload[0] <= ZZB_ZCL_IAS_ACE_PANEL_ARMED_AWAY) {
		sample_led(cmd->payload[0] != ZZB_ZCL_IAS_ACE_PANEL_DISARMED);
	}

	return ZZB_ZCL_SUCCESS;
}
#endif

#if defined(CONFIG_SAMPLE_IAS_CIE)
/* The panel: disarmed or armed away (all zones) */
static atomic_t armed;

/* Burglar with strobe and medium siren for 10 s, strobe 50 % at medium level (ZCL Figure 8-21) */
static const uint8_t burglar_warning[] = {
	FIELD_PREP(ZZB_ZCL_IAS_WD_WARNING_MODE, 1) | FIELD_PREP(ZZB_ZCL_IAS_WD_STROBE, 1) |
		FIELD_PREP(ZZB_ZCL_IAS_WD_SIREN_LEVEL, 1),
	10,
	0,
	50,
	1,
};

/* Panel Status, Seconds Remaining, Audible Notification and Alarm Status (ZCL Figure 8-15) */
static void panel_status(uint8_t status[4])
{
	status[0] = atomic_get(&armed) ? ZZB_ZCL_IAS_ACE_PANEL_ARMED_AWAY
				       : ZZB_ZCL_IAS_ACE_PANEL_DISARMED;
	status[1] = 0;
	status[2] = ZZB_ZCL_IAS_ACE_AUDIBLE_DEFAULT;
	status[3] = ZZB_ZCL_IAS_ACE_ALARM_NONE;
}

/* ZCL 8.3.2.4.5: every change of the panel goes to the bound keypads */
static void panel_set(struct zzb_node *node, bool on)
{
	uint8_t status[4];
	int err;

	atomic_set(&armed, on);
	sample_led(on);
	LOG_INF("panel %s", on ? "armed" : "disarmed");
	panel_status(status);
	err = zzb_zcl_send_bound(node, IAS_EP, ZZB_ZCL_IAS_ACE,
				 ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED, status, sizeof(status));
	if (err) {
		LOG_ERR("Panel Status Changed %d", err);
	}
}

static void respond(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, uint8_t id,
		    const uint8_t *payload, size_t len)
{
	int err = zzb_zcl_cmd_respond(node, cmd, id, payload, len);

	if (err) {
		LOG_ERR("response 0x%02x %d", id, err);
	}
}

/* Every Arm Mode other than Disarm arms all zones; Arm Response gives the result */
static uint8_t arm(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	bool on = cmd->payload[0] != ZZB_ZCL_IAS_ACE_MODE_DISARM;
	uint8_t notification =
		on ? ZZB_ZCL_IAS_ACE_ALL_ZONES_ARMED : ZZB_ZCL_IAS_ACE_ALL_ZONES_DISARMED;

	if (cmd->payload[0] > ZZB_ZCL_IAS_ACE_MODE_ALL_ZONES) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	respond(node, cmd, ZZB_ZCL_IAS_ACE_ARM_RSP, &notification, sizeof(notification));
	if (on != atomic_get(&armed)) {
		panel_set(node, on);
	}

	return ZZB_ZCL_SUCCESS;
}

/* Zone Enroll Response with code 0 (success) and a new zone ID */
static uint8_t enroll(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	static uint8_t next_id;
	const uint8_t rsp[] = {0x00, next_id};

	next_id++;
	LOG_INF("zone 0x%04x type 0x%04x ID %u", cmd->src_addr, sys_get_le16(cmd->payload), rsp[1]);
	respond(node, cmd, ZZB_ZCL_IAS_ZONE_ENROLL_RSP, rsp, sizeof(rsp));
	return ZZB_ZCL_SUCCESS;
}

/* Zone Status Change Notification: an alarm of an armed panel starts the warning devices */
static uint8_t zone_status(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint16_t status = sys_get_le16(cmd->payload);
	int err;

	LOG_INF("zone ID %u status 0x%04x", cmd->payload[3], status);
	if (!atomic_get(&armed) ||
	    !(status & (ZZB_ZCL_IAS_ZONE_STATUS_ALARM1 | ZZB_ZCL_IAS_ZONE_STATUS_ALARM2))) {
		return ZZB_ZCL_SUCCESS;
	}

	err = zzb_zcl_send_bound(node, IAS_EP, ZZB_ZCL_IAS_WD, ZZB_ZCL_IAS_WD_START_WARNING,
				 burglar_warning, sizeof(burglar_warning));
	if (err) {
		LOG_ERR("Start Warning %d", err);
	}

	return ZZB_ZCL_SUCCESS;
}

static uint8_t cie_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	uint8_t status[4];

	if (cmd->cluster == ZZB_ZCL_IAS_ZONE) {
		return cmd->id == ZZB_ZCL_IAS_ZONE_ENROLL_REQ ? enroll(node, cmd)
							      : zone_status(node, cmd);
	}

	switch (cmd->id) {
	case ZZB_ZCL_IAS_ACE_ARM:
		return arm(node, cmd);
	case ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS:
		panel_status(status);
		respond(node, cmd, ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS_RSP, status, sizeof(status));
		return ZZB_ZCL_SUCCESS;
	default:
		/* The panel has no zone table, bypass or emergency state */
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		blink_update();
		break;
#if defined(CONFIG_SAMPLE_ZONE)
	case ZZB_ZCL_IAS_ZONE:
		atomic_set(&zone_id, zone.zone_id);
		LOG_INF("zone state %u ID %u status 0x%04x", zone.zone_state, zone.zone_id,
			zone.zone_status);
		break;
#endif
	default:
		break;
	}
}

static uint8_t zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
#if defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
	return wd_cmd(cmd);
#elif defined(CONFIG_SAMPLE_IAS_ACE)
	return keypad_cmd(cmd);
#elif defined(CONFIG_SAMPLE_IAS_CIE)
	return cie_cmd(node, cmd);
#else
	/* The IAS Zone server passes no command */
	return ZZB_ZCL_UNSUP_COMMAND;
#endif
}

void sample_button(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_IAS_ACE)
	keypad_arm(node);
#elif defined(CONFIG_SAMPLE_IAS_CIE)
	panel_set(node, !atomic_get(&armed));
#else
	zone_alarm(node);
#endif
}

/* The DTL forbids finding & binding of the IAS clusters, so the target only identifies */
void sample_finding_binding(struct zzb_node *node)
{
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, IAS_EP));
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_cmd_cb = zcl_cmd,
		.sleepy = IS_ENABLED(CONFIG_SAMPLE_SLEEPY),
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

#if defined(CONFIG_SAMPLE_ZONE)
	/* The enrollment comes from the settings without zcl_cb */
	atomic_set(&zone_id, zone.zone_id);
#endif
	return zzb_bdb_start(node);
}
