/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
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

#if defined(CONFIG_SAMPLE_IAS_ZONE)
#define DEVICE_ID ZZB_DEVICE_IAS_ZONE
#define ZONE_TYPE ZZB_ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH
#elif defined(CONFIG_SAMPLE_IAS_WARNING_DEVICE)
#define DEVICE_ID ZZB_DEVICE_IAS_WD
#define ZONE_TYPE ZZB_ZCL_IAS_ZONE_TYPE_STANDARD_WARNING_DEVICE
#elif defined(CONFIG_SAMPLE_IAS_ACE)
#define DEVICE_ID ZZB_DEVICE_IAS_ACE
#define ZONE_TYPE ZZB_ZCL_IAS_ZONE_TYPE_KEYPAD
#elif defined(CONFIG_SAMPLE_IAS_CIE)
#define DEVICE_ID ZZB_DEVICE_IAS_CIE
#endif

/* The warning that an alarm of an armed panel starts: 10 s, the strobe on 50 % of each second */
#define WARNING_DURATION  10
#define STROBE_DUTY_CYCLE 50

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

#if defined(CONFIG_SAMPLE_SLEEPY)
/* Two AA cells of 1.5 V, at 3.0 V and 100 % */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
#endif

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_ZONE)
static struct zzb_zcl_ias_zone zone = {
	.zone_type = ZONE_TYPE,
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
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
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
/* Stop is warning mode Stop without strobe; another command with a duration of 0 changes nothing */
static uint8_t start_warning(const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_ias_wd_start_warning start;

	zzb_zcl_ias_wd_start_warning_parse(cmd, &start);
	LOG_INF("warning mode %u strobe %u for %u s", start.warning_mode, start.strobe,
		start.warning_duration);
	if (start.warning_mode == ZZB_ZCL_IAS_WD_WARNING_MODE_STOP &&
	    start.strobe == ZZB_ZCL_IAS_WD_STROBE_NONE) {
		warning = false;
	} else if (start.warning_duration > 0) {
		warning = true;
	}

	blink_update();
	return ZZB_ZCL_SUCCESS;
}

static uint8_t wd_cmd(const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_ias_wd_squawk squawk;

	if (cmd->id == ZZB_ZCL_IAS_WD_START_WARNING) {
		return start_warning(cmd);
	}

	zzb_zcl_ias_wd_squawk_parse(cmd, &squawk);
	LOG_INF("squawk mode %u strobe %u level %u", squawk.squawk_mode, squawk.strobe,
		squawk.squawk_level);
	return ZZB_ZCL_SUCCESS;
}
#endif

#if defined(CONFIG_SAMPLE_IAS_ACE)
/* The code of the panel of ZHA */
static const uint8_t code[] = {'1', '2', '3', '4'};

/* Arm with Arm All Zones and with Disarm in turn */
static void keypad_arm(struct zzb_node *node)
{
	static bool disarm;
	int err = zzb_zcl_ias_ace_arm(
		node, IAS_EP, disarm ? ZZB_ZCL_IAS_ACE_MODE_DISARM : ZZB_ZCL_IAS_ACE_MODE_ALL_ZONES,
		code, sizeof(code), atomic_get(&zone_id));

	if (err) {
		LOG_ERR("Arm %d", err);
		return;
	}

	LOG_INF("%s sent", disarm ? "disarm" : "arm");
	disarm = !disarm;
}

/* led0 is on while some zones are armed; the other values leave it */
static uint8_t keypad_cmd(const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_ias_ace_arm_rsp rsp;
	struct zzb_zcl_ias_ace_panel_status_changed panel;

	if (cmd->id == ZZB_ZCL_IAS_ACE_ARM_RSP) {
		zzb_zcl_ias_ace_arm_rsp_parse(cmd, &rsp);
		LOG_INF("arm notification %u", rsp.arm_notification);
		if (rsp.arm_notification <= ZZB_ZCL_IAS_ACE_ALL_ZONES_ARMED) {
			sample_led(rsp.arm_notification != ZZB_ZCL_IAS_ACE_ALL_ZONES_DISARMED);
		}

		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->id != ZZB_ZCL_IAS_ACE_PANEL_STATUS_CHANGED) {
		return ZZB_ZCL_SUCCESS;
	}

	zzb_zcl_ias_ace_panel_status_changed_parse(cmd, &panel);
	LOG_INF("panel status %u", panel.panel_status);
	if (panel.panel_status <= ZZB_ZCL_IAS_ACE_PANEL_ARMED_AWAY) {
		sample_led(panel.panel_status != ZZB_ZCL_IAS_ACE_PANEL_DISARMED);
	}

	return ZZB_ZCL_SUCCESS;
}
#endif

#if defined(CONFIG_SAMPLE_IAS_CIE)
/* The panel: disarmed or armed away (all zones) */
static atomic_t armed;

static enum zzb_zcl_ias_ace_panel_status panel_status(void)
{
	return atomic_get(&armed) ? ZZB_ZCL_IAS_ACE_PANEL_ARMED_AWAY
				  : ZZB_ZCL_IAS_ACE_PANEL_DISARMED;
}

/* ZCL 8.3.2.4.5: every change of the panel goes to the bound keypads */
static void panel_set(struct zzb_node *node, bool on)
{
	int err;

	atomic_set(&armed, on);
	sample_led(on);
	LOG_INF("panel %s", on ? "armed" : "disarmed");
	err = zzb_zcl_ias_ace_panel_status_changed(node, IAS_EP, panel_status(), 0,
						   ZZB_ZCL_IAS_ACE_AUDIBLE_DEFAULT,
						   ZZB_ZCL_IAS_ACE_ALARM_NONE);
	if (err) {
		LOG_ERR("Panel Status Changed %d", err);
	}
}

/* Every Arm Mode other than Disarm arms all zones; Arm Response gives the result */
static uint8_t arm(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_ias_ace_arm request;
	bool on;
	int err;

	zzb_zcl_ias_ace_arm_parse(cmd, &request);
	if (request.arm_mode > ZZB_ZCL_IAS_ACE_MODE_ALL_ZONES) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	on = request.arm_mode != ZZB_ZCL_IAS_ACE_MODE_DISARM;
	err = zzb_zcl_ias_ace_arm_rsp(node, cmd,
				      on ? ZZB_ZCL_IAS_ACE_ALL_ZONES_ARMED
					 : ZZB_ZCL_IAS_ACE_ALL_ZONES_DISARMED);
	if (err) {
		LOG_ERR("Arm Response %d", err);
	}

	if (on != atomic_get(&armed)) {
		panel_set(node, on);
	}

	return ZZB_ZCL_SUCCESS;
}

/* Every zone is enrolled with a new zone ID */
static uint8_t enroll(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	static uint8_t next_id;
	struct zzb_zcl_ias_zone_enroll_req request;
	int err;

	zzb_zcl_ias_zone_enroll_req_parse(cmd, &request);
	LOG_INF("zone 0x%04x type 0x%04x ID %u", cmd->src_addr, request.zone_type, next_id);
	err = zzb_zcl_ias_zone_enroll_rsp(node, cmd, ZZB_ZCL_IAS_ZONE_ENROLL_SUCCESS, next_id++);
	if (err) {
		LOG_ERR("Zone Enroll Response %d", err);
	}

	return ZZB_ZCL_SUCCESS;
}

/* An alarm of an armed panel starts a burglar warning of the bound warning devices */
static uint8_t zone_status(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_ias_zone_status_change_notification change;
	int err;

	zzb_zcl_ias_zone_status_change_notification_parse(cmd, &change);
	LOG_INF("zone ID %u status 0x%04x", change.zone_id, change.zone_status);
	if (!atomic_get(&armed) || !(change.zone_status & (ZZB_ZCL_IAS_ZONE_STATUS_ALARM1 |
							   ZZB_ZCL_IAS_ZONE_STATUS_ALARM2))) {
		return ZZB_ZCL_SUCCESS;
	}

	err = zzb_zcl_ias_wd_start_warning(node, IAS_EP, ZZB_ZCL_IAS_WD_WARNING_MODE_BURGLAR,
					   ZZB_ZCL_IAS_WD_STROBE_PARALLEL,
					   ZZB_ZCL_IAS_WD_SIREN_LEVEL_MEDIUM, WARNING_DURATION,
					   STROBE_DUTY_CYCLE, ZZB_ZCL_IAS_WD_STROBE_LEVEL_MEDIUM);
	if (err) {
		LOG_ERR("Start Warning %d", err);
	}

	return ZZB_ZCL_SUCCESS;
}

static uint8_t cie_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	int err;

	if (cmd->cluster == ZZB_ZCL_IAS_ZONE) {
		return cmd->id == ZZB_ZCL_IAS_ZONE_ENROLL_REQ ? enroll(node, cmd)
							      : zone_status(node, cmd);
	}

	switch (cmd->id) {
	case ZZB_ZCL_IAS_ACE_ARM:
		return arm(node, cmd);
	case ZZB_ZCL_IAS_ACE_GET_PANEL_STATUS:
		err = zzb_zcl_ias_ace_get_panel_status_rsp(node, cmd, panel_status(), 0,
							   ZZB_ZCL_IAS_ACE_AUDIBLE_DEFAULT,
							   ZZB_ZCL_IAS_ACE_ALARM_NONE);
		if (err) {
			LOG_ERR("Get Panel Status Response %d", err);
		}

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
