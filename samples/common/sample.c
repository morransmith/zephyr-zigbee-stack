/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "sample.h"

LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);

#define FINDING_BINDING_HOLD_MS 3000
#define RESET_HOLD_MS           10000
#define IDENTIFY_BLINK_MS       250

#define BUTTON DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

static struct zzb_node *sample_node;
static atomic_t on_network;
static atomic_t led_state;
static atomic_t identifying;

static const char *const event_names[] = {
	"init",
	"steering",
	"rejoin",
	"left",
	"finding & binding target",
	"finding & binding initiator",
};

static const char *const status_names[] = {
	"success",
	"no network",
	"TCLK exchange failure",
	"not on a network",
	"no Identify Query Response",
	"binding table full",
};

static void blink(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(blink_work, blink);

static void blink(struct k_work *work)
{
	static bool phase;

	if (!atomic_get(&identifying)) {
		gpio_pin_set_dt(&led, atomic_get(&led_state));
		return;
	}

	phase = !phase;
	gpio_pin_set_dt(&led, phase);
	k_work_schedule(&blink_work, K_MSEC(IDENTIFY_BLINK_MS));
}

void sample_led(bool on)
{
	atomic_set(&led_state, on);
	if (gpio_is_ready_dt(&led) && !atomic_get(&identifying)) {
		gpio_pin_set_dt(&led, on);
	}
}

void sample_identify(bool on)
{
	if (!gpio_is_ready_dt(&led) || atomic_set(&identifying, on) == on) {
		return;
	}

	k_work_schedule(&blink_work, K_NO_WAIT);
}

bool sample_on_network(void)
{
	return atomic_get(&on_network);
}

void sample_bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	LOG_INF("%s: %s", event_names[event], status_names[status]);

	switch (event) {
	case ZZB_BDB_EVENT_STEERING:
		atomic_set(&on_network, status == ZZB_BDB_SUCCESS);
		break;
	case ZZB_BDB_EVENT_INIT:
	case ZZB_BDB_EVENT_REJOIN:
		/* A node whose rejoin fails stays on its network and rejoins again later */
		atomic_set(&on_network, status != ZZB_BDB_NOT_ON_A_NETWORK);
		break;
	case ZZB_BDB_EVENT_LEFT:
		atomic_clear(&on_network);
		break;
	default:
		break;
	}

	if (event == ZZB_BDB_EVENT_LEFT ||
	    (event == ZZB_BDB_EVENT_INIT && status == ZZB_BDB_NOT_ON_A_NETWORK)) {
		LOG_INF("steering %d", zzb_bdb_steering(node));
	}
}

/* The action follows from the time between the press and the release */
static void button(struct input_event *evt, void *user_data)
{
	static int64_t pressed;
	int64_t held;

	if (evt->type != INPUT_EV_KEY || evt->code != DT_PROP(BUTTON, zephyr_code) ||
	    !sample_node) {
		return;
	}

	if (evt->value) {
		pressed = k_uptime_get();
		return;
	}

	held = k_uptime_get() - pressed;
	if (held >= RESET_HOLD_MS) {
		LOG_INF("reset %d", zzb_bdb_reset(sample_node));
		return;
	}

	if (held >= FINDING_BINDING_HOLD_MS) {
		sample_finding_binding(sample_node);
		return;
	}

	if (!atomic_get(&on_network)) {
		LOG_INF("steering %d", zzb_bdb_steering(sample_node));
		return;
	}

	sample_button(sample_node);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PARENT(BUTTON)), button, NULL);

int sample_init(const struct zzb_node_config *cfg, struct zzb_node **node)
{
	int err;

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	err = zzb_node_init(cfg, node);
	if (err) {
		LOG_ERR("node init %d", err);
		return err;
	}

	sample_node = *node;
	return 0;
}
