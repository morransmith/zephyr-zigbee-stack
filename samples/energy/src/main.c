/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zzb/zcl/appliance_control.h>
#include <zzb/zcl/appliance_events.h>
#include <zzb/zcl/appliance_id.h>
#include <zzb/zcl/appliance_stats.h>
#include <zzb/zcl/basic.h>
#include <zzb/zcl/electrical_measurement.h>
#include <zzb/zcl/groups.h>
#include <zzb/zcl/identify.h>
#include <zzb/zcl/meter_id.h>
#include <zzb/zcl/metering.h>
#include <zzb/zcl/on_off.h>
#include <zzb/zcl/power_profile.h>
#include <zzb/zcl/scenes.h>
#include <zzb/zcl/time.h>
#include <zzb/zzb.h>

#include "sample.h"

LOG_MODULE_REGISTER(energy, LOG_LEVEL_INF);

#define ENERGY_EP 1

#if defined(CONFIG_SAMPLE_SMART_PLUG)
#define DEVICE_ID ZZB_DEVICE_SMART_PLUG
#elif defined(CONFIG_SAMPLE_METER_INTERFACE)
#define DEVICE_ID ZZB_DEVICE_METER_INTERFACE
#elif defined(CONFIG_SAMPLE_CONSUMPTION_AWARENESS_DEVICE)
#define DEVICE_ID ZZB_DEVICE_CONSUMPTION_AWARENESS
#elif defined(CONFIG_SAMPLE_WHITE_GOODS)
#define DEVICE_ID ZZB_DEVICE_WHITE_GOODS
#elif defined(CONFIG_SAMPLE_HOME_GATEWAY)
#define DEVICE_ID ZZB_DEVICE_HOME_GATEWAY
#endif

/* The load of the Smart Plug: 60 W at 230 V, metered every 10 s */
#define PLUG_VOLTAGE    230
#define PLUG_CURRENT_MA 261
#define PLUG_POWER      60
#define PLUG_PERIOD_S   10

/* The meters count pulses of 10 L or 10 Wh, one a minute */
#define METER_PULSE    10
#define METER_PERIOD_S 60

/* The program of the washing machine and its energy phase */
#define PROGRAM_MINUTES 2
#define PHASE_PEAK_W    2000
/* 50.0 Wh with the one decimal of EnergyFormatting */
#define PHASE_ENERGY    500

static struct zzb_zcl_basic basic = {
	.manufacturer_name = "zzb",
	.model_id = CONFIG_SAMPLE_MODEL_ID,
	.power_source = ZZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE,
	.generic_device_class = ZZB_ZCL_BASIC_GENERIC_DEVICE_CLASS_UNSPECIFIED,
	.generic_device_type = ZZB_ZCL_BASIC_GENERIC_DEVICE_TYPE_UNSPECIFIED,
};

static struct zzb_zcl_identify identify;

#if defined(CONFIG_SAMPLE_SMART_PLUG)
static struct zzb_zcl_scenes scenes;

static struct zzb_zcl_on_off on_off = {
	.start_up_default = ZZB_ZCL_START_UP_PREVIOUS,
};

/* RMSCurrent in mA, RMSVoltage and the power in V and W */
static struct zzb_zcl_electrical_measurement measurement = {
	.measurement_type = ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_ACTIVE |
			    ZZB_ZCL_ELECTRICAL_MEASUREMENT_TYPE_PHASE_A,
	.ac_frequency = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
	.ac_frequency_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
	.ac_frequency_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_AC_FREQUENCY_UNKNOWN,
	.ac_frequency_multiplier = 1,
	.ac_frequency_divisor = 1,
	.rms_voltage = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
	.rms_voltage_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
	.rms_voltage_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_UNKNOWN,
	.rms_current = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
	.rms_current_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
	.rms_current_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT_UNKNOWN,
	.active_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
	.active_power_min = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
	.active_power_max = ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_UNKNOWN,
	.reactive_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_REACTIVE_POWER_UNKNOWN,
	.apparent_power = ZZB_ZCL_ELECTRICAL_MEASUREMENT_APPARENT_POWER_UNKNOWN,
	.ac_voltage_multiplier = 1,
	.ac_voltage_divisor = 1,
	.ac_current_multiplier = 1,
	.ac_current_divisor = 1000,
	.ac_power_multiplier = 1,
	.ac_power_divisor = 1,
};
#endif

#if defined(CONFIG_SAMPLE_METER_INTERFACE)
/* A gas meter in m3 with three decimals */
static struct zzb_zcl_metering metering = {
	.unit_of_measure = ZZB_ZCL_METERING_UNIT_OF_MEASURE_M3,
	.multiplier = 1,
	.divisor = 1000,
	.metering_device_type = ZZB_ZCL_METERING_DEVICE_TYPE_GAS,
};

static struct zzb_zcl_meter_id meter_id = {
	.company_name = "zzb",
	.pod = "0",
};
#elif defined(CONFIG_SAMPLE_METERING)
/* kWh and kW with three decimals */
static struct zzb_zcl_metering metering = {
	.unit_of_measure = ZZB_ZCL_METERING_UNIT_OF_MEASURE_KWH,
	.multiplier = 1,
	.divisor = 1000,
	.summation_formatting = ZZB_ZCL_METERING_FORMATTING(5, 3, false),
	.demand_formatting = ZZB_ZCL_METERING_FORMATTING(5, 3, false),
	.metering_device_type = ZZB_ZCL_METERING_DEVICE_TYPE_ELECTRIC,
};
#endif

#if defined(CONFIG_SAMPLE_WHITE_GOODS)
/* One power profile; Energy in Wh with one decimal */
static struct zzb_zcl_power_profile power_profile = {
	.total_profile_num = 1,
	.energy_formatting = ZZB_ZCL_POWER_PROFILE_FORMATTING(0, 1, false),
};

static struct zzb_zcl_appliance_control appliance_control;
static struct zzb_zcl_appliance_id appliance_id;

/* Logs of up to 60 octets */
static struct zzb_zcl_appliance_stats appliance_stats = {
	.log_max_size = 60,
	.log_queue_max_size = 1,
};
#endif

#if defined(CONFIG_SAMPLE_HOME_GATEWAY)
static struct zzb_zcl_time gateway_time;
#endif

static const struct zzb_zcl_cluster energy[] = {
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_IDENTIFY_CLIENT(),
#if defined(CONFIG_SAMPLE_SMART_PLUG)
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
	ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(&measurement),
	ZZB_ZCL_TIME_CLIENT(),
#endif
#if defined(CONFIG_SAMPLE_METERING)
	ZZB_ZCL_METERING_SERVER(&metering),
#endif
#if defined(CONFIG_SAMPLE_METER_INTERFACE)
	ZZB_ZCL_METER_ID_SERVER(&meter_id),
#endif
#if defined(CONFIG_SAMPLE_WHITE_GOODS)
	ZZB_ZCL_POWER_PROFILE_SERVER(&power_profile),
	ZZB_ZCL_APPLIANCE_CONTROL_SERVER(&appliance_control),
	ZZB_ZCL_APPLIANCE_ID_SERVER(&appliance_id),
	ZZB_ZCL_APPLIANCE_EVENTS_SERVER(),
	ZZB_ZCL_APPLIANCE_STATS_SERVER(&appliance_stats),
#endif
#if defined(CONFIG_SAMPLE_HOME_GATEWAY)
	ZZB_ZCL_TIME_SERVER(&gateway_time),
	ZZB_ZCL_POWER_PROFILE_CLIENT(),
	ZZB_ZCL_METERING_CLIENT(),
	ZZB_ZCL_METER_ID_CLIENT(),
	ZZB_ZCL_APPLIANCE_CONTROL_CLIENT(),
	ZZB_ZCL_APPLIANCE_ID_CLIENT(),
	ZZB_ZCL_APPLIANCE_EVENTS_CLIENT(),
	ZZB_ZCL_APPLIANCE_STATS_CLIENT(),
#endif
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = ENERGY_EP,
		.profile = ZZB_PROFILE_HA,
		.device_id = DEVICE_ID,
		.device_version = ZZB_DEVICE_VERSION(DEVICE_ID),
		.clusters = energy,
		.cluster_count = ARRAY_SIZE(energy),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

/* The node for the timers, set in main() */
static struct zzb_node *timer_node;

#if defined(CONFIG_SAMPLE_METERING)
static void attr_set(struct zzb_node *node, uint16_t cluster, uint16_t attr, const void *value)
{
	int err = zzb_zcl_attr_set(node, ENERGY_EP, cluster, attr, value);

	if (err) {
		LOG_ERR("attribute 0x%04x of cluster 0x%04x %d", attr, cluster, err);
	}
}

static void meter_tick(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(meter_work, meter_tick);
#endif

#if defined(CONFIG_SAMPLE_SMART_PLUG)
/* OnOff for the button and the timer */
static atomic_t relay_on;

/* The measurements follow the relay; RMSVoltage is that of the mains */
static void relay_set(struct zzb_node *node, bool on)
{
	uint16_t voltage = PLUG_VOLTAGE;
	uint16_t current = on ? PLUG_CURRENT_MA : 0;
	int16_t power = on ? PLUG_POWER : 0;
	int32_t demand = power;

	atomic_set(&relay_on, on);
	sample_led(on);
	LOG_INF("relay %s", on ? "on" : "off");
	attr_set(node, ZZB_ZCL_ELECTRICAL_MEASUREMENT, ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE,
		 &voltage);
	attr_set(node, ZZB_ZCL_ELECTRICAL_MEASUREMENT, ZZB_ZCL_ELECTRICAL_MEASUREMENT_RMS_CURRENT,
		 &current);
	attr_set(node, ZZB_ZCL_ELECTRICAL_MEASUREMENT, ZZB_ZCL_ELECTRICAL_MEASUREMENT_ACTIVE_POWER,
		 &power);
	attr_set(node, ZZB_ZCL_METERING, ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND, &demand);
}

static void plug_toggle(struct zzb_node *node)
{
	bool on = !atomic_get(&relay_on);
	int err = zzb_zcl_attr_set(node, ENERGY_EP, ZZB_ZCL_ON_OFF, ZZB_ZCL_ON_OFF_ON_OFF, &on);

	if (err) {
		LOG_ERR("OnOff %d", err);
		return;
	}

	relay_set(node, on);
}

/* CurrentSummationDelivered counts Wh: the energy of each period while the relay is on */
static void meter_tick(struct k_work *work)
{
	static uint64_t energy_ws;
	uint64_t summation;

	k_work_schedule(&meter_work, K_SECONDS(PLUG_PERIOD_S));
	if (!atomic_get(&relay_on)) {
		return;
	}

	energy_ws += PLUG_POWER * PLUG_PERIOD_S;
	summation = energy_ws / SEC_PER_HOUR;
	attr_set(timer_node, ZZB_ZCL_METERING, ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED,
		 &summation);
}
#endif

#if defined(CONFIG_SAMPLE_METER)
/* CurrentSummationDelivered for the button and the timer */
static atomic_t summation;

static void meter_add(struct zzb_node *node)
{
	atomic_val_t total = atomic_add(&summation, METER_PULSE) + METER_PULSE;
	uint64_t value = total;

	LOG_INF("summation %ld", total);
	attr_set(node, ZZB_ZCL_METERING, ZZB_ZCL_METERING_CURRENT_SUMMATION_DELIVERED, &value);
}

static void meter_tick(struct k_work *work)
{
	meter_add(timer_node);
	k_work_schedule(&meter_work, K_SECONDS(METER_PERIOD_S));
}

/* InstantaneousDemand is the rate of the timer, one pulse a minute */
static void meter_start(struct zzb_node *node)
{
	int32_t demand = METER_PULSE * MIN_PER_HOUR;

	attr_set(node, ZZB_ZCL_METERING, ZZB_ZCL_METERING_INSTANTANEOUS_DEMAND, &demand);
	k_work_schedule(&meter_work, K_SECONDS(METER_PERIOD_S));
}
#endif

#if defined(CONFIG_SAMPLE_WHITE_GOODS)
/* Appliance Status: idle, running, paused or finished (ZCL Table 15-9) */
static atomic_t program = ATOMIC_INIT(ZZB_ZCL_APPLIANCE_IDLE);

/* The power profile and its energy phase, the whole program */
#define PROFILE_ID 1
#define PHASE_ID   1

static const struct zzb_zcl_power_profile_phase phase = {
	.energy_phase_id = PHASE_ID,
	.macro_phase_id = PHASE_ID,
	.expected_duration = PROGRAM_MINUTES,
	.peak_power = PHASE_PEAK_W,
	.energy = PHASE_ENERGY,
	.max_activation_delay = ZZB_ZCL_POWER_PROFILE_ACTIVATION_DELAY_FIRST,
};

static void program_end(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(program_work, program_end);

static void check(int err, const char *response)
{
	if (err) {
		LOG_ERR("%s %d", response, err);
	}
}

/* ZCL 15.2.4.1.2: a new state goes as Signal State Notification */
static void program_set(struct zzb_node *node, enum zzb_zcl_appliance_status status)
{
	int err;

	if (atomic_set(&program, status) == status) {
		return;
	}

	if (status == ZZB_ZCL_APPLIANCE_RUNNING) {
		k_work_schedule(&program_work, K_MINUTES(PROGRAM_MINUTES));
	} else {
		k_work_cancel_delayable(&program_work);
	}

	sample_led(status == ZZB_ZCL_APPLIANCE_RUNNING);
	LOG_INF("appliance status 0x%02x", status);
	err = zzb_zcl_appliance_control_signal_state_notification(
		node, ENERGY_EP, status, ZZB_ZCL_APPLIANCE_REMOTE_CONTROL,
		ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_PROPRIETARY_0);
	check(err, "Signal State Notification");
}

static void program_end(struct k_work *work)
{
	program_set(timer_node, ZZB_ZCL_APPLIANCE_END_PROGRAMMED);
}

static enum zzb_zcl_power_profile_state profile_state(void)
{
	switch (atomic_get(&program)) {
	case ZZB_ZCL_APPLIANCE_RUNNING:
		return ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_RUNNING;
	case ZZB_ZCL_APPLIANCE_PAUSED:
		return ZZB_ZCL_POWER_PROFILE_ENERGY_PHASE_PAUSED;
	case ZZB_ZCL_APPLIANCE_END_PROGRAMMED:
		return ZZB_ZCL_POWER_PROFILE_ENDED;
	default:
		return ZZB_ZCL_POWER_PROFILE_PROGRAMMED;
	}
}

static uint8_t profile_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	const struct zzb_zcl_power_profile_record record = {
		.power_profile_id = PROFILE_ID,
		.energy_phase_id = PHASE_ID,
		.remote_control = power_profile.energy_remote,
		.state = profile_state(),
	};
	struct zzb_zcl_power_profile_request request;

	switch (cmd->id) {
	case ZZB_ZCL_POWER_PROFILE_REQUEST:
		zzb_zcl_power_profile_request_parse(cmd, &request);
		if (request.power_profile_id != ZZB_ZCL_POWER_PROFILE_ID_ALL &&
		    request.power_profile_id != PROFILE_ID) {
			return ZZB_ZCL_NOT_FOUND;
		}

		check(zzb_zcl_power_profile_rsp(node, cmd, power_profile.total_profile_num,
						PROFILE_ID, &phase, 1),
		      "PowerProfileResponse");
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_POWER_PROFILE_STATE_REQUEST:
		check(zzb_zcl_power_profile_state_rsp(node, cmd, &record, 1),
		      "PowerProfileStateResponse");
		return ZZB_ZCL_SUCCESS;
	default:
		/* The appliance has no prices and no schedules */
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* Execution of a Command (ZCL 15.2.4.1); Pause holds a running program */
static uint8_t execute(struct zzb_node *node, enum zzb_zcl_appliance_command command)
{
	switch (command) {
	case ZZB_ZCL_APPLIANCE_START:
		program_set(node, ZZB_ZCL_APPLIANCE_RUNNING);
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_APPLIANCE_STOP:
		program_set(node, ZZB_ZCL_APPLIANCE_IDLE);
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_APPLIANCE_PAUSE:
		if (atomic_get(&program) != ZZB_ZCL_APPLIANCE_RUNNING) {
			return ZZB_ZCL_FAILURE;
		}

		program_set(node, ZZB_ZCL_APPLIANCE_PAUSED);
		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_INVALID_FIELD;
	}
}

static uint8_t control_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	struct zzb_zcl_appliance_control_execution_of_command execution;

	switch (cmd->id) {
	case ZZB_ZCL_APPLIANCE_CONTROL_EXECUTION_OF_COMMAND:
		zzb_zcl_appliance_control_execution_of_command_parse(cmd, &execution);
		return execute(node, execution.command);
	case ZZB_ZCL_APPLIANCE_CONTROL_SIGNAL_STATE:
		check(zzb_zcl_appliance_control_signal_state_rsp(
			      node, cmd, atomic_get(&program), ZZB_ZCL_APPLIANCE_REMOTE_CONTROL,
			      ZZB_ZCL_APPLIANCE_DEVICE_STATUS2_PROPRIETARY_0),
		      "Signal State Response");
		return ZZB_ZCL_SUCCESS;
	default:
		/* Write Functions and the overload commands */
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* The appliance has no alerts and no logs (ZCL 15.4.2.4.1, 15.5.3.2.1.3) */
static uint8_t goods_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	switch (cmd->cluster) {
	case ZZB_ZCL_POWER_PROFILE:
		return profile_cmd(node, cmd);
	case ZZB_ZCL_APPLIANCE_CONTROL:
		return control_cmd(node, cmd);
	case ZZB_ZCL_APPLIANCE_EVENTS:
		check(zzb_zcl_appliance_events_get_alerts_rsp(node, cmd, NULL, 0),
		      "Get Alerts Response");
		return ZZB_ZCL_SUCCESS;
	case ZZB_ZCL_APPLIANCE_STATS:
		if (cmd->id == ZZB_ZCL_APPLIANCE_STATS_LOG_REQUEST) {
			return ZZB_ZCL_NOT_FOUND;
		}

		check(zzb_zcl_appliance_stats_log_queue_rsp(node, cmd, NULL, 0),
		      "Log Queue Response");
		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}
#endif

#if defined(CONFIG_SAMPLE_HOME_GATEWAY)
static void gateway_start(struct zzb_node *node)
{
	int err = zzb_zcl_appliance_control_execution_of_command(node, ENERGY_EP,
								 ZZB_ZCL_APPLIANCE_START);

	if (err) {
		LOG_ERR("Execution of a Command %d", err);
		return;
	}

	LOG_INF("start sent");
}

/* The gateway has no prices and no scheduler for the requests of a Power Profile server */
static uint8_t gateway_cmd(const struct zzb_zcl_cmd *cmd)
{
	LOG_INF("cluster 0x%04x command 0x%02x from 0x%04x", cmd->cluster, cmd->id, cmd->src_addr);
	LOG_HEXDUMP_INF(cmd->payload, cmd->len, "payload");
	if (cmd->cluster != ZZB_ZCL_POWER_PROFILE) {
		return ZZB_ZCL_SUCCESS;
	}

	switch (cmd->id) {
	case ZZB_ZCL_POWER_PROFILE_GET_PRICE:
	case ZZB_ZCL_POWER_PROFILE_GET_OVERALL_SCHEDULE_PRICE:
	case ZZB_ZCL_POWER_PROFILE_ENERGY_PHASES_SCHEDULE_REQUEST:
	case ZZB_ZCL_POWER_PROFILE_GET_PRICE_EXTENDED:
		return ZZB_ZCL_UNSUP_COMMAND;
	default:
		return ZZB_ZCL_SUCCESS;
	}
}
#endif

static void zcl_changed(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	switch (cluster) {
	case ZZB_ZCL_IDENTIFY:
		sample_identify(identify.identify_time > 0);
		break;
#if defined(CONFIG_SAMPLE_SMART_PLUG)
	case ZZB_ZCL_ON_OFF:
		if (attr == ZZB_ZCL_ON_OFF_ON_OFF) {
			relay_set(node, on_off.on_off);
		}
		break;
#endif
	default:
		break;
	}
}

static uint8_t zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
#if defined(CONFIG_SAMPLE_WHITE_GOODS)
	return goods_cmd(node, cmd);
#elif defined(CONFIG_SAMPLE_HOME_GATEWAY)
	return gateway_cmd(cmd);
#else
	/* Metering, Meter Identification and Electrical Measurement pass no command */
	return ZZB_ZCL_UNSUP_COMMAND;
#endif
}

void sample_button(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_SMART_PLUG)
	plug_toggle(node);
#elif defined(CONFIG_SAMPLE_METER)
	meter_add(node);
#elif defined(CONFIG_SAMPLE_WHITE_GOODS)
	program_set(node, atomic_get(&program) == ZZB_ZCL_APPLIANCE_RUNNING
				  ? ZZB_ZCL_APPLIANCE_IDLE
				  : ZZB_ZCL_APPLIANCE_RUNNING);
#else
	gateway_start(node);
#endif
}

void sample_finding_binding(struct zzb_node *node)
{
#if defined(CONFIG_SAMPLE_HOME_GATEWAY)
	LOG_INF("finding & binding %d",
		zzb_bdb_finding_binding_initiator(node, ENERGY_EP, ZZB_BDB_FB_UNICAST));
#else
	LOG_INF("finding & binding %d", zzb_bdb_finding_binding_target(node, ENERGY_EP));
#endif
}

int main(void)
{
	const struct zzb_node_config cfg = {
		.radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154)),
		.device = &device,
		.bdb_cb = sample_bdb_event,
		.zcl_cb = zcl_changed,
		.zcl_cmd_cb = zcl_cmd,
	};
	struct zzb_node *node;
	int err = sample_init(&cfg, &node);

	if (err) {
		return err;
	}

	timer_node = node;
#if defined(CONFIG_SAMPLE_SMART_PLUG)
	/* StartUpOnOff has set OnOff without zcl_cb */
	relay_set(node, on_off.on_off);
	k_work_schedule(&meter_work, K_SECONDS(PLUG_PERIOD_S));
#elif defined(CONFIG_SAMPLE_METER)
	meter_start(node);
#endif
	return zzb_bdb_start(node);
}
