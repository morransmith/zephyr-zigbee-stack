/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zzb/zcl/ias_zone.h>

#include "aps/aps.h"
#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 8.2.1.1) */
#define REVISION 2

/* ZoneState (ZCL Table 8-4) */
#define NOT_ENROLLED 0x00
#define ENROLLED     0x01

/* ZoneID before enrollment (ZCL Table 8-8) */
#define NO_ZONE 0xff

/* Enroll response code Success (ZCL Table 8-10) */
#define ENROLL_SUCCESS 0x00

/* The default of NumberOfZoneSensitivityLevelsSupported (ZCL Table 8-8) */
#define SENSITIVITY_LEVELS 2

/* A notification refused off a network or during a rejoin is sent again after this */
#define RETRY_MS 5000

/* The delay of a notification is in quarter seconds (ZCL 8.2.2.3.1.5) */
#define DELAY_MS  250
#define DELAY_MAX 0xffff

#define RECORD_VERSION 1
/* Version and the endpoint of the CIE */
#define RECORD_LEN     2
/* Room for zcl/ias/<ep> */
#define NAME_LEN       12

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_STATE, ZZB_ZCL_ENUM8, ZZB_ZCL_NV, struct zzb_zcl_ias_zone,
		     zone_state),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_TYPE, ZZB_ZCL_ENUM16, 0, struct zzb_zcl_ias_zone, zone_type),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_STATUS, ZZB_ZCL_MAP16, 0, struct zzb_zcl_ias_zone,
		     zone_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_CIE_ADDRESS, ZZB_ZCL_EUI64, ZZB_ZCL_WRITE | ZZB_ZCL_NV,
		     struct zzb_zcl_ias_zone, ias_cie_address),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_ID, ZZB_ZCL_UINT8, ZZB_ZCL_NV, struct zzb_zcl_ias_zone,
		     zone_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_NUMBER_OF_SENSITIVITY_LEVELS_SUPPORTED, ZZB_ZCL_UINT8, 0,
		     struct zzb_zcl_ias_zone, number_of_zone_sensitivity_levels_supported),
	ZZB_ZCL_ATTR(ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL, ZZB_ZCL_UINT8,
		     ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct zzb_zcl_ias_zone,
		     current_zone_sensitivity_level),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

static bool has_cie(const struct zzb_zcl_ias_zone *v)
{
	return v->ias_cie_address != 0 && v->ias_cie_address != UINT64_MAX;
}

static void set(struct zzb_node *node, uint8_t ep, uint16_t attr, uint32_t value)
{
	zzb_zcl_set(node, ep, ZZB_ZCL_IAS_ZONE, attr, value);
}

static void record_name(char name[NAME_LEN], uint8_t ep)
{
	snprintk(name, NAME_LEN, "zcl/ias/%u", ep);
}

/* A failed save is ignored */
static void save(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_ias_zone *v)
{
	const uint8_t rec[RECORD_LEN] = {RECORD_VERSION, v->cie_ep};
	char name[NAME_LEN];

	record_name(name, ep);
	zzb_settings_save(node, name, rec, sizeof(rec));
}

/* Sends the command id with the payload from the server on ep to the CIE */
static int send_to_cie(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_ias_zone *v,
		       uint8_t id, const uint8_t *payload, size_t len)
{
	return zzb_zcl_send_ext(node, zzb_node_endpoint(node, ep), v->ias_cie_address, v->cie_ep,
				ZZB_ZCL_IAS_ZONE, id, payload, len);
}

/* ZCL 8.2.2.3.2; a sleepy zone polls fast for Zone Enroll Response */
static int enroll_request(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_ias_zone *v)
{
	uint8_t payload[4];
	int err;

	sys_put_le16(v->zone_type, payload);
	sys_put_le16(node->device->manufacturer_code, payload + 2);
	err = send_to_cie(node, ep, v, ZZB_ZCL_IAS_ZONE_ENROLL_REQ, payload, sizeof(payload));
	if (err == 0) {
		zzb_nwk_poll_fast(node);
	}

	return err;
}

/*
 * Zone Status Change Notification (ZCL 8.2.2.3.1), or Zone Enroll Request before enrollment
 * (trip-to-pair, ZCL 8.2.2.1.3); nothing without a CIE
 */
static int send_status(struct zzb_node *node, uint8_t ep, const struct zzb_zcl_ias_zone *v,
		       int64_t now)
{
	uint8_t payload[6];

	if (!has_cie(v)) {
		return 0;
	}

	if (v->zone_state != ENROLLED) {
		return enroll_request(node, ep, v);
	}

	sys_put_le16(v->zone_status, payload);
	payload[2] = 0;
	payload[3] = v->zone_id;
	sys_put_le16(MIN((now - v->since) / DELAY_MS, DELAY_MAX), payload + 4);
	return send_to_cie(node, ep, v, ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION, payload,
			   sizeof(payload));
}

/* Sends a change of ZoneStatus; a refusal off a network or during a rejoin waits for a retry */
static void notify(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ias_zone *v)
{
	int64_t now = k_uptime_get();
	int err;

	if (v->retry == 0) {
		v->since = now;
	}

	err = send_status(node, ep, v, now);
	/* Other errors, such as no buffer, count as sent */
	v->retry = err == -ENOTCONN || err == -EBUSY ? now + RETRY_MS : 0;
}

/* The values of the IAS Zone server on ep, or NULL */
static struct zzb_zcl_ias_zone *server(const struct zzb_endpoint *ep)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_IAS_ZONE, true);

	return cl != NULL ? cl->values : NULL;
}

/* The time t of a timer event, or INT64_MAX for 0, which stands for none */
static int64_t due(int64_t t)
{
	return t != 0 ? t : INT64_MAX;
}

/* Arms the timer for the earliest retry or end of the test mode of the node, or stops it */
static void schedule(struct zzb_node *node)
{
	int64_t next = INT64_MAX;

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_zcl_ias_zone *v = server(&node->device->endpoints[i]);

		if (v != NULL) {
			next = MIN(next, MIN(due(v->retry), due(v->test_end)));
		}
	}

	if (next == INT64_MAX) {
		k_work_cancel_delayable(&node->zcl.ias_zone_timer);
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.ias_zone_timer,
				    K_MSEC(MAX(next - k_uptime_get(), 0)));
}

/* Ends the test mode; the change of bit 8 of ZoneStatus is sent (ZCL 8.2.2.2.2.2) */
static void end_test(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ias_zone *v)
{
	v->test_end = 0;
	set(node, ep, ZZB_ZCL_IAS_ZONE_STATUS, v->zone_status & ~ZZB_ZCL_IAS_ZONE_STATUS_TEST);
}

static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.ias_zone_timer);
	int64_t now = k_uptime_get();

	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];
		struct zzb_zcl_ias_zone *v = server(ep);

		if (v != NULL && due(v->test_end) <= now) {
			end_test(node, ep->id, v);
		}

		if (v != NULL && due(v->retry) <= now) {
			notify(node, ep->id, v);
		}
	}

	schedule(node);
}

/*
 * A new IAS_CIE_Address ends the enrollment. A write of it gives the endpoint of the CIE and starts
 * auto-enroll-request (ZCL 8.2.2.1.3); the reset of the node gives no endpoint.
 */
static void cie_changed(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ias_zone *v)
{
	const struct zzb_zcl_cmd *write = node->zcl.received;

	set(node, ep, ZZB_ZCL_IAS_ZONE_STATE, NOT_ENROLLED);
	set(node, ep, ZZB_ZCL_IAS_ZONE_ID, NO_ZONE);
	if (write == NULL) {
		return;
	}

	v->cie_ep = write->src_ep;
	save(node, ep, v);
	if (has_cie(v)) {
		enroll_request(node, ep, v);
	}
}

/* The stack keeps bit 8 (Test), also in a value of the application, and sends the change */
static void status_changed(struct zzb_node *node, uint8_t ep, struct zzb_zcl_ias_zone *v)
{
	v->zone_status &= ~ZZB_ZCL_IAS_ZONE_STATUS_TEST;
	if (v->test_end != 0) {
		v->zone_status |= ZZB_ZCL_IAS_ZONE_STATUS_TEST;
	}

	notify(node, ep, v);
	schedule(node);
}

static void changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	switch (attr) {
	case ZZB_ZCL_IAS_ZONE_CIE_ADDRESS:
		cie_changed(node, ep, values);
		break;
	case ZZB_ZCL_IAS_ZONE_STATUS:
		status_changed(node, ep, values);
		break;
	}
}

/* ZCL 8.2.2.2.1: the response code and the zone ID */
static int enroll_response(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			   struct zzb_zcl_ias_zone *v)
{
	bool success = cmd->payload[0] == ENROLL_SUCCESS;

	set(node, cmd->ep, ZZB_ZCL_IAS_ZONE_STATE, success ? ENROLLED : NOT_ENROLLED);
	set(node, cmd->ep, ZZB_ZCL_IAS_ZONE_ID, success ? cmd->payload[1] : NO_ZONE);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 8.2.2.2.2.2 */
static int initiate_normal_mode(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
				struct zzb_zcl_ias_zone *v)
{
	end_test(node, cmd->ep, v);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 8.2.2.2.2.4: the duration in seconds and the sensitivity level */
static int initiate_test_mode(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			      struct zzb_zcl_ias_zone *v)
{
	uint8_t duration = cmd->payload[0];
	uint8_t level = cmd->payload[1];

	if (level > v->number_of_zone_sensitivity_levels_supported) {
		return ZZB_ZCL_INVALID_FIELD;
	}

	set(node, cmd->ep, ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL, level);
	v->test_end = k_uptime_get() + duration * MSEC_PER_SEC;
	set(node, cmd->ep, ZZB_ZCL_IAS_ZONE_STATUS, v->zone_status | ZZB_ZCL_IAS_ZONE_STATUS_TEST);
	if (duration == 0) {
		end_test(node, cmd->ep, v);
	}

	/* A new duration of a running test mode */
	schedule(node);
	return ZZB_ZCL_SUCCESS;
}

/* The commands with the length of their fields */
static const struct {
	uint8_t id;
	uint8_t len;
	int (*fn)(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ias_zone *v);
} commands[] = {
	{ZZB_ZCL_IAS_ZONE_ENROLL_RSP, 2, enroll_response},
	{ZZB_ZCL_IAS_ZONE_INITIATE_NORMAL_MODE, 0, initiate_normal_mode},
	{ZZB_ZCL_IAS_ZONE_INITIATE_TEST_MODE, 2, initiate_test_mode},
};

/* ZCL 8.2.2.1.3: the source is the network address of IAS_CIE_Address */
static bool authorized(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		       const struct zzb_zcl_ias_zone *v)
{
	uint16_t addr;

	return has_cie(v) && zzb_aps_resolve_ext(node, v->ias_cie_address, &addr) == 0 &&
	       addr == cmd->src_addr;
}

static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	ARRAY_FOR_EACH(commands, i) {
		if (commands[i].id != cmd->id) {
			continue;
		}

		if (!authorized(node, cmd, values)) {
			return ZZB_ZCL_NOT_AUTHORIZED;
		}

		if (cmd->len < commands[i].len) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		return commands[i].fn(node, cmd, values);
	}

	return ZZB_ZCL_UNSUP_COMMAND;
}

/* ZCL 8.2.2.1.2.4: level 0 is the default of the device and may equal another level */
static uint8_t check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	const struct zzb_zcl_ias_zone *v = values;

	if (attr->id == ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL &&
	    value > v->number_of_zone_sensitivity_levels_supported) {
		return ZZB_ZCL_INVALID_VALUE;
	}

	return ZZB_ZCL_SUCCESS;
}

/*
 * The defaults of ZCL 8.2.2.1 outside the test mode; the fields of the application keep their
 * values
 */
static void init(void *values)
{
	struct zzb_zcl_ias_zone *v = values;

	v->zone_state = NOT_ENROLLED;
	v->zone_status &= ~ZZB_ZCL_IAS_ZONE_STATUS_TEST;
	v->ias_cie_address = 0;
	v->zone_id = NO_ZONE;
	if (v->number_of_zone_sensitivity_levels_supported == 0) {
		v->number_of_zone_sensitivity_levels_supported = SENSITIVITY_LEVELS;
	}

	v->current_zone_sensitivity_level = 0;
	v->cie_ep = 0;
	v->retry = 0;
	v->test_end = 0;
}

static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_ias_zone *v = values;
	char name[NAME_LEN];

	set(node, ep, ZZB_ZCL_IAS_ZONE_STATE, NOT_ENROLLED);
	set(node, ep, ZZB_ZCL_IAS_ZONE_ID, NO_ZONE);
	set(node, ep, ZZB_ZCL_IAS_ZONE_CIE_ADDRESS, 0);
	set(node, ep, ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL, 0);
	end_test(node, ep, v);
	v->cie_ep = 0;
	/* Not to the next CIE */
	v->retry = 0;
	schedule(node);
	record_name(name, ep);
	zzb_settings_delete(node, name);
}

const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.reset = reset,
	.cmd = cmd,
	.check = check,
	.changed = changed,
};

static const struct zzb_zcl_attr client_attrs[] = {
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The commands that the client passes to zcl_cmd_cb, with the length of their fields */
static const struct {
	uint8_t id;
	uint8_t len;
} client_commands[] = {
	{ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION, 6},
	{ZZB_ZCL_IAS_ZONE_ENROLL_REQ, 4},
};

static int client_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	ARRAY_FOR_EACH(client_commands, i) {
		if (client_commands[i].id != cmd->id) {
			continue;
		}

		if (cmd->len < client_commands[i].len) {
			return ZZB_ZCL_MALFORMED_COMMAND;
		}

		return zzb_zcl_app_cmd(node, cmd);
	}

	return ZZB_ZCL_UNSUP_COMMAND;
}

const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_client_def = {
	.attrs = client_attrs,
	.attr_count = ARRAY_SIZE(client_attrs),
	.cmd = client_cmd,
};

int zzb_zcl_ias_zone_load(struct zzb_node *node, int ep, size_t len, settings_read_cb read_cb,
			  void *cb_arg)
{
	const struct zzb_endpoint *e =
		IN_RANGE(ep, 0, UINT8_MAX) ? zzb_node_endpoint(node, ep) : NULL;
	const struct zzb_zcl_cluster *cl =
		e != NULL ? zzb_zcl_find_cluster(e, ZZB_ZCL_IAS_ZONE, true) : NULL;
	struct zzb_zcl_ias_zone *v;
	uint8_t rec[RECORD_LEN];
	int err;

	if (cl == NULL) {
		return 0;
	}

	err = zzb_settings_read(len, read_cb, cb_arg, rec, sizeof(rec));
	if (err) {
		return err;
	}

	v = cl->values;
	v->cie_ep = rec[1];
	return 0;
}

void zzb_zcl_ias_zone_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.ias_zone_timer, tick);
}
