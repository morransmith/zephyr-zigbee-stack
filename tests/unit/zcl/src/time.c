/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/time.h>

#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/* A second node, whose Time server does not change the local time of the test node */
#define TIME_EUI64 0x00124b0001020305ULL
#define SERVER_EP  110

static struct zzb_zcl_time time110;

static const struct zzb_zcl_cluster ep110_clusters[] = {
	ZZB_ZCL_TIME_SERVER(&time110),
};

static const struct zzb_endpoint time_endpoints[] = {
	{
		.id = SERVER_EP,
		.profile = 0x0104,
		.device_id = 0x0050,
		.clusters = ep110_clusters,
		.cluster_count = ARRAY_SIZE(ep110_clusters),
	},
};

static const struct zzb_device time_device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = time_endpoints,
	.endpoint_count = ARRAY_SIZE(time_endpoints),
};

static struct zzb_node *time_node;

/* Passes a frame of the Time cluster from the requester to endpoint ep of the second node */
static void receive_time(uint8_t ep, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ep,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_TIME,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(time_node, buf, &ind);
}

struct reboot_call {
	struct k_work work;
	int err;
};

static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_zcl_init(time_node);
	call->err = zzb_settings_load(time_node);
	if (call->err == 0) {
		zzb_zcl_start(time_node);
	}
}

/* Starts ZCL of the second node again from its records, as reboot() */
static int reboot_time(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void reset_handler(struct k_work *work)
{
	zzb_zcl_reset(time_node);
}

/* Resets the ZCL attributes of the second node, as reset_node() */
static void reset_time(void)
{
	struct k_work work;

	zzb_call_sync(&work, reset_handler);
}

static void set_status(uint8_t value)
{
	zassert_ok(
		zzb_zcl_attr_set(time_node, SERVER_EP, ZZB_ZCL_TIME, ZZB_ZCL_TIME_STATUS, &value));
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = TIME_EUI64,
		.device = &time_device,
	};

	test_node();
	zassert_ok(zzb_node_init(&cfg, &time_node));
	return NULL;
}

static void before(void *fixture)
{
	reset_node();
	reset_time();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* Section 16: the clock of the node runs from its setting and the reset of the node clears it */
ZTEST(zzb_zcl_time, test_clock)
{
	struct zzb_node *node = test_node();
	uint32_t utc;
	uint32_t local;

	zassert_equal(zzb_zcl_time_get(node, &utc, &local), -ENODATA);
	zassert_equal(zzb_zcl_clock_utc(node), 0xffffffff);
	zassert_equal(zzb_zcl_clock_local(node), 0xffffffff);

	zzb_zcl_clock_set(node, 1000, -3600);
	k_sleep(K_MSEC(5500));
	zassert_ok(zzb_zcl_time_get(node, &utc, &local));
	zassert_equal(utc, 1005);
	zassert_equal(local, 1005 - 3600);
	zassert_equal(zzb_zcl_clock_utc(node), 1005);
	zassert_equal(zzb_zcl_clock_local(node), 1005 - 3600);

	reset_node();
	zassert_equal(zzb_zcl_time_get(node, &utc, &local), -ENODATA);
}

/*
 * ZCL 3.12.2.2: without time Time, StandardTime, LocalTime and LastSetTime read 0xffffffff. A write
 * of Time sets the clock with its offset and LastSetTime. TimeZone 3600 s and DstShift 3600 s from
 * Time 0x20000003 to 0x20000010 give StandardTime and LocalTime, which is also the local time of
 * the node.
 */
ZTEST(zzb_zcl_time, test_server_time)
{
	static const uint8_t read[] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x06,
				       0x00, 0x07, 0x00, 0x08, 0x00};
	static const uint8_t no_time_rsp[] = {0x18, 0x40, 0x01, 0x00, 0x00, 0x00, 0xe2, 0xff, 0xff,
					      0xff, 0xff, 0x06, 0x00, 0x00, 0x23, 0xff, 0xff, 0xff,
					      0xff, 0x07, 0x00, 0x00, 0x23, 0xff, 0xff, 0xff, 0xff,
					      0x08, 0x00, 0x00, 0xe2, 0xff, 0xff, 0xff, 0xff};
	static const uint8_t write_dst[] = {0x00, 0x41, 0x02, 0x02, 0x00, 0x2b, 0x10, 0x0e,
					    0x00, 0x00, 0x03, 0x00, 0x23, 0x03, 0x00, 0x00,
					    0x20, 0x04, 0x00, 0x23, 0x10, 0x00, 0x00, 0x20,
					    0x05, 0x00, 0x2b, 0x10, 0x0e, 0x00, 0x00};
	static const uint8_t write_time[] = {0x00, 0x42, 0x02, 0x00, 0x00,
					     0xe2, 0x00, 0x00, 0x00, 0x20};
	static const uint8_t reread[] = {0x00, 0x43, 0x00, 0x00, 0x00, 0x06,
					 0x00, 0x07, 0x00, 0x08, 0x00};
	static const uint8_t reread_rsp[] = {0x18, 0x43, 0x01, 0x00, 0x00, 0x00, 0xe2, 0x05, 0x00,
					     0x00, 0x20, 0x06, 0x00, 0x00, 0x23, 0x15, 0x0e, 0x00,
					     0x20, 0x07, 0x00, 0x00, 0x23, 0x25, 0x1c, 0x00, 0x20,
					     0x08, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00, 0x20};
	uint32_t utc;
	uint32_t local;

	receive_time(SERVER_EP, read, sizeof(read));
	check_frame(0, SERVER_EP, ZZB_ZCL_TIME, no_time_rsp, sizeof(no_time_rsp));

	zzb_zcl_clock_set(time_node, 100, 1800);
	receive_time(SERVER_EP, write_dst, sizeof(write_dst));
	check_frame(1, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x41, 0x04, 0x00}, 4);
	receive_time(SERVER_EP, write_time, sizeof(write_time));
	check_frame(2, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x42, 0x04, 0x00}, 4);
	zassert_equal(time_node->zcl.clock.offset, 1800);
	zassert_ok(zzb_zcl_time_get(time_node, &utc, &local));
	zassert_equal(utc, 0x20000000);
	zassert_equal(local, 0x20000000 + 3600);

	k_sleep(K_MSEC(5500));
	receive_time(SERVER_EP, reread, sizeof(reread));
	check_frame(3, SERVER_EP, ZZB_ZCL_TIME, reread_rsp, sizeof(reread_rsp));
	zassert_ok(zzb_zcl_time_get(time_node, &utc, &local));
	zassert_equal(utc, 0x20000005);
	zassert_equal(local, 0x20000005 + 7200);

	k_sleep(K_SECONDS(12));
	zassert_ok(zzb_zcl_time_get(time_node, &utc, &local));
	zassert_equal(utc, 0x20000011);
	zassert_equal(local, 0x20000011 + 3600);
}

/*
 * ZCL 3.12.2.2.1: while Master is set a remote write of Time gets READ_ONLY, and the application
 * sets the clock. 0xffffffff is not a time.
 */
ZTEST(zzb_zcl_time, test_server_master)
{
	static const uint8_t write[] = {0x00, 0x40, 0x02, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00, 0x20};
	static const uint8_t no_time[] = {0x00, 0x41, 0x02, 0x00, 0x00,
					  0xe2, 0xff, 0xff, 0xff, 0xff};
	uint32_t time = 0x20000000;
	uint32_t not_valid = 0xffffffff;

	set_status(ZZB_ZCL_TIME_MASTER);
	receive_time(SERVER_EP, write, sizeof(write));
	check_frame(0, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x40, 0x04, 0x88, 0x00, 0x00}, 6);
	receive_time(SERVER_EP, no_time, sizeof(no_time));
	check_frame(1, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x41, 0x04, 0x87, 0x00, 0x00}, 6);
	zassert_equal(zzb_zcl_clock_utc(time_node), 0xffffffff);

	zassert_ok(zzb_zcl_attr_set(time_node, SERVER_EP, ZZB_ZCL_TIME, ZZB_ZCL_TIME_TIME, &time));
	zassert_equal(zzb_zcl_clock_utc(time_node), 0x20000000);
	zassert_equal(time110.last_set_time, 0x20000000);
	zassert_equal(
		zzb_zcl_attr_set(time_node, SERVER_EP, ZZB_ZCL_TIME, ZZB_ZCL_TIME_TIME, &not_valid),
		-EINVAL);
	zassert_equal(zzb_zcl_clock_utc(time_node), 0x20000000);
}

/*
 * ZCL Table 3-70: a remote write of TimeStatus changes only Synchronized and Superseding, and
 * Synchronized stays 0 while Master is set
 */
ZTEST(zzb_zcl_time, test_server_status)
{
	static const uint8_t write[] = {0x00, 0x40, 0x02, 0x01, 0x00, 0x18, 0x0b};
	static const uint8_t rewrite[] = {0x00, 0x41, 0x02, 0x01, 0x00, 0x18, 0x0e};

	set_status(ZZB_ZCL_TIME_MASTER_ZONE_DST);
	receive_time(SERVER_EP, write, sizeof(write));
	check_frame(0, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x40, 0x04, 0x00}, 4);
	zassert_equal(time110.time_status, 0x0e);

	set_status(ZZB_ZCL_TIME_MASTER);
	zassert_equal(time110.time_status, 0x01);
	receive_time(SERVER_EP, rewrite, sizeof(rewrite));
	zassert_equal(time110.time_status, 0x09);
}

/*
 * ZCL 3.12.2.2.3 to 3.12.2.2.6: TimeZone and DstShift range from -86400 to 86400, and while
 * MasterZoneDst is set a remote write of TimeZone or the DST attributes gets READ_ONLY after the
 * range check
 */
ZTEST(zzb_zcl_time, test_server_zone_dst)
{
	static const uint8_t outside[] = {0x00, 0x40, 0x02, 0x02, 0x00, 0x2b, 0x81, 0x51, 0x01,
					  0x00, 0x05, 0x00, 0x2b, 0x7f, 0xae, 0xfe, 0xff};
	static const uint8_t outside_rsp[] = {0x18, 0x40, 0x04, 0x87, 0x02, 0x00, 0x87, 0x05, 0x00};
	static const uint8_t limits[] = {0x00, 0x41, 0x02, 0x02, 0x00, 0x2b, 0x80, 0x51, 0x01,
					 0x00, 0x05, 0x00, 0x2b, 0x80, 0xae, 0xfe, 0xff};
	static const uint8_t master[] = {0x00, 0x42, 0x02, 0x02, 0x00, 0x2b, 0x81, 0x51, 0x01, 0x00,
					 0x03, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x23,
					 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x2b, 0x00, 0x00, 0x00,
					 0x00, 0x09, 0x00, 0xe2, 0x00, 0x00, 0x00, 0x30};
	static const uint8_t master_rsp[] = {0x18, 0x42, 0x04, 0x87, 0x02, 0x00, 0x88, 0x03,
					     0x00, 0x88, 0x04, 0x00, 0x88, 0x05, 0x00};
	int32_t zone = 3600;
	int32_t too_far = 86401;

	receive_time(SERVER_EP, outside, sizeof(outside));
	check_frame(0, SERVER_EP, ZZB_ZCL_TIME, outside_rsp, sizeof(outside_rsp));
	receive_time(SERVER_EP, limits, sizeof(limits));
	check_frame(1, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x41, 0x04, 0x00}, 4);
	zassert_equal(time110.time_zone, 86400);
	zassert_equal(time110.dst_shift, -86400);

	set_status(ZZB_ZCL_TIME_MASTER_ZONE_DST);
	receive_time(SERVER_EP, master, sizeof(master));
	check_frame(2, SERVER_EP, ZZB_ZCL_TIME, master_rsp, sizeof(master_rsp));
	zassert_equal(time110.time_zone, 86400);
	zassert_equal(time110.dst_start, 0xffffffff);
	zassert_equal(time110.valid_until_time, 0x30000000);

	zassert_ok(zzb_zcl_attr_set(time_node, SERVER_EP, ZZB_ZCL_TIME, ZZB_ZCL_TIME_ZONE, &zone));
	zassert_equal(time110.time_zone, 3600);
	zassert_equal(
		zzb_zcl_attr_set(time_node, SERVER_EP, ZZB_ZCL_TIME, ZZB_ZCL_TIME_ZONE, &too_far),
		-EINVAL);
}

/*
 * TimeZone, the DST attributes and ValidUntilTime are saved, while the clock and LastSetTime are
 * not. The reset of the node restores the defaults of ZCL Table 3-69.
 */
ZTEST(zzb_zcl_time, test_server_settings)
{
	static const uint8_t write[] = {0x00, 0x40, 0x02, 0x02, 0x00, 0x2b, 0x10, 0x0e, 0x00,
					0x00, 0x03, 0x00, 0x23, 0x03, 0x00, 0x00, 0x20, 0x04,
					0x00, 0x23, 0x10, 0x00, 0x00, 0x20, 0x05, 0x00, 0x2b,
					0x10, 0x0e, 0x00, 0x00, 0x09, 0x00, 0xe2, 0x00, 0x00,
					0x00, 0x30, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00, 0x20};

	receive_time(SERVER_EP, write, sizeof(write));
	check_frame(0, SERVER_EP, ZZB_ZCL_TIME, (uint8_t[]){0x18, 0x40, 0x04, 0x00}, 4);
	zassert_ok(reboot_time());
	zassert_equal(time110.time_zone, 3600);
	zassert_equal(time110.dst_start, 0x20000003);
	zassert_equal(time110.dst_end, 0x20000010);
	zassert_equal(time110.dst_shift, 3600);
	zassert_equal(time110.valid_until_time, 0x30000000);
	zassert_equal(time110.last_set_time, 0xffffffff);
	zassert_equal(zzb_zcl_clock_utc(time_node), 0xffffffff);

	set_status(ZZB_ZCL_TIME_MASTER | ZZB_ZCL_TIME_MASTER_ZONE_DST);
	reset_time();
	zassert_ok(reboot_time());
	zassert_equal(time110.time_status, 0);
	zassert_equal(time110.time_zone, 0);
	zassert_equal(time110.dst_start, 0xffffffff);
	zassert_equal(time110.dst_end, 0xffffffff);
	zassert_equal(time110.dst_shift, 0);
	zassert_equal(time110.valid_until_time, 0xffffffff);
}

ZTEST_SUITE(zzb_zcl_time, NULL, setup, before, after, NULL);
