/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/power_config.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zcl/window_covering.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(covering, LOG_LEVEL_INF);

#define COVERING_EP 1

#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
#define DEVICE_ID ZZB_DEVICE_WINDOW_COVERING
#elif defined(CONFIG_SAMPLE_WINDOW_COVERING_CONTROLLER)
#define DEVICE_ID ZZB_DEVICE_WINDOW_COVERING_CONTROLLER
#endif

/* Lift and tilt in percent */
#define OPEN   0
#define CLOSED 100

/* The motor moves 10 % a second */
#define MOTOR_STEP      10
#define MOTOR_STEP_TIME K_SECONDS(1)

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = SAMPLE_BASIC_POWER_SOURCE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_SLEEPY)
/* 3.0 V, 100 %, two AA cells */
static struct zzb_zcl_power_config power_config = {
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_size = ZZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
#endif

#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
static struct zzb_zcl_scenes scenes;

/* The stack does not save the positions, so the covering starts open; the limits are in cm */
static struct zzb_zcl_window_covering covering = {
	.window_covering_type = ZZB_ZCL_WINDOW_COVERING_TYPE_TILT_BLIND_LIFT_AND_TILT,
	.config_status = ZZB_ZCL_WINDOW_COVERING_CONFIG_OPERATIONAL |
			 ZZB_ZCL_WINDOW_COVERING_CONFIG_ONLINE |
			 ZZB_ZCL_WINDOW_COVERING_CONFIG_LIFT_CLOSED_LOOP |
			 ZZB_ZCL_WINDOW_COVERING_CONFIG_TILT_CLOSED_LOOP,
	.installed_closed_limit_lift = 200,
	.installed_closed_limit_tilt = 900,
};
#endif

static const struct zzb_zcl_cluster clusters[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
#if defined(CONFIG_SAMPLE_SLEEPY)
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
#endif
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_WINDOW_COVERING_SERVER(&covering),
#else
	ZZB_ZCL_IDENTIFY_CLIENT(),
	ZZB_ZCL_GROUPS_CLIENT(),
	ZZB_ZCL_SCENES_CLIENT(),
	ZZB_ZCL_WINDOW_COVERING_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = COVERING_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = clusters,
		.cluster_count = ARRAY_SIZE(clusters),
	},
};

static const struct zzb_device device = {
	.power_source = SAMPLE_POWER_SOURCE,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
static struct zzb_node *motor_node;

/* Positions and targets in percent, for the motor, the button and zcl_cmd_cb */
static atomic_t lift;
static atomic_t tilt;
static atomic_t lift_target;
static atomic_t tilt_target;

static void motor_run(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(motor_work, motor_run);

/* Moves one axis a step toward its target and returns whether the target is still ahead */
static bool motor_step(atomic_t *position, const atomic_t *target, uint16_t attr)
{
	uint8_t from = atomic_get(position);
	uint8_t to = atomic_get(target);
	uint8_t next = from < to ? MIN(from + MOTOR_STEP, to) : MAX(from - MOTOR_STEP, to);
	int err;

	if (next == from) {
		return false;
	}

	err = zzb_zcl_attr_set(motor_node, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, attr, &next);
	if (err) {
		LOG_ERR("position %d", err);
		return false;
	}

	atomic_set(position, next);
	return next != to;
}

static void motor_run(struct k_work *work)
{
	bool lift_moves = motor_step(&lift, &lift_target, ZZB_ZCL_WINDOW_COVERING_LIFT_PERCENTAGE);
	bool tilt_moves = motor_step(&tilt, &tilt_target, ZZB_ZCL_WINDOW_COVERING_TILT_PERCENTAGE);

	LOG_INF("lift %ld %% tilt %ld %%", atomic_get(&lift), atomic_get(&tilt));
	sample_led(lift_moves || tilt_moves);
	if (lift_moves || tilt_moves) {
		k_work_schedule(&motor_work, MOTOR_STEP_TIME);
	}
}

static void motor_move(uint8_t lift_percentage, uint8_t tilt_percentage)
{
	atomic_set(&lift_target, lift_percentage);
	atomic_set(&tilt_target, tilt_percentage);
	k_work_schedule(&motor_work, K_NO_WAIT);
}

static void motor_stop(void)
{
	motor_move(atomic_get(&lift), atomic_get(&tilt));
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	if (cluster == ZZB_ZCL_IDENTIFY) {
		sample_identify(identify.identify_time > 0);
	}
}

#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
/* A recalled scene comes as Go To Lift Percentage and Go To Tilt Percentage */
static uint8_t covering_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_window_covering_go_to_lift_percentage go_to_lift;
	struct zzb_zcl_window_covering_go_to_tilt_percentage go_to_tilt;

	switch (cmd->id) {
	case ZZB_ZCL_WINDOW_COVERING_UP_OPEN:
		motor_move(OPEN, OPEN);
		break;
	case ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE:
		motor_move(CLOSED, CLOSED);
		break;
	case ZZB_ZCL_WINDOW_COVERING_STOP:
		motor_stop();
		break;
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
		zzb_zcl_window_covering_go_to_lift_percentage_parse(cmd, &go_to_lift);
		motor_move(go_to_lift.percentage, atomic_get(&tilt_target));
		break;
	case ZZB_ZCL_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE:
		zzb_zcl_window_covering_go_to_tilt_percentage_parse(cmd, &go_to_tilt);
		motor_move(atomic_get(&lift_target), go_to_tilt.percentage);
		break;
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	return ZZB_ZCL_SUCCESS;
}

/* Up/Open, Stop, Down/Close, Stop in turn */
void sample_button(struct zzb_node *node)
{
	static unsigned int presses;

	switch (presses++ % 4) {
	case 0:
		motor_move(OPEN, OPEN);
		break;
	case 2:
		motor_move(CLOSED, CLOSED);
		break;
	default:
		motor_stop();
		break;
	}
}
#else
static const uint8_t button_cmds[] = {
	ZZB_ZCL_WINDOW_COVERING_UP_OPEN,
	ZZB_ZCL_WINDOW_COVERING_STOP,
	ZZB_ZCL_WINDOW_COVERING_DOWN_CLOSE,
	ZZB_ZCL_WINDOW_COVERING_STOP,
};

void sample_button(struct zzb_node *node)
{
	static unsigned int presses;
	uint8_t cmd = button_cmds[presses++ % ARRAY_SIZE(button_cmds)];
	int err = zzb_zcl_send_bound(node, COVERING_EP, ZZB_ZCL_WINDOW_COVERING, cmd, NULL, 0);

	if (err) {
		LOG_ERR("command 0x%02x %d", cmd, err);
		return;
	}

	LOG_INF("command 0x%02x", cmd);
}
#endif

void sample_finding_binding(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, COVERING_EP));
#else
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, COVERING_EP, ZZB_BDB_FB_UNICAST));
#endif
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
		.zcl_cmd_cb = covering_cmd,
#endif
		.sleepy = IS_ENABLED(CONFIG_SAMPLE_SLEEPY),
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

#if defined(CONFIG_SAMPLE_WINDOW_COVERING)
	motor_node = node;
#endif
	return zzb_bdb_start(node);
}
