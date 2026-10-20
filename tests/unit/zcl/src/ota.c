/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/ota.h>

#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

/*
 * A fourth node, whose queries stay out of the other suites. Endpoint 170 has the OTA Upgrade
 * client that downloads, endpoint 171 a second client.
 */
#define OTA_EUI64        0x00124b0001020307ULL
#define OTA_EP           170
#define SECOND_EP        171
#define OTA_MANUFACTURER 0x1234
#define OTA_IMAGE_TYPE   0x0042
#define OTA_VERSION      0x01020300

static struct zzb_zcl_ota ota170 = {
	.current_file_version = OTA_VERSION,
	.image_type = OTA_IMAGE_TYPE,
};

static struct zzb_zcl_ota ota171;

static const struct zzb_zcl_cluster ep170_clusters[] = {
	ZZB_ZCL_OTA_CLIENT(&ota170),
};

static const struct zzb_zcl_cluster ep171_clusters[] = {
	ZZB_ZCL_OTA_CLIENT(&ota171),
};

static const struct zzb_endpoint ota_endpoints[] = {
	{
		.id = OTA_EP,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep170_clusters,
		.cluster_count = ARRAY_SIZE(ep170_clusters),
	},
	{
		.id = SECOND_EP,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep171_clusters,
		.cluster_count = ARRAY_SIZE(ep171_clusters),
	},
};

static const struct zzb_device ota_device = {
	.manufacturer_code = OTA_MANUFACTURER,
	.power_source = ZZB_POWER_MAINS,
	.endpoints = ota_endpoints,
	.endpoint_count = ARRAY_SIZE(ota_endpoints),
};

static struct zzb_node *ota_node;

/* The query interval in milliseconds */
#define INTERVAL_MS (CONFIG_ZZB_ZCL_OTA_QUERY_INTERVAL * 60000LL)

/* A node other than the trust center, which runs the OTA server */
#define OTHER_ADDR 0x1234

/* Query Next Image Request of the client: manufacturer code, image type and CurrentFileVersion */
static const uint8_t query_req[] = {0x00, 0x34, 0x12, 0x42, 0x00, 0x00, 0x03, 0x02, 0x01};

/*
 * Passes a frame of OTA Upgrade from src_addr and src_ep to dst_addr and endpoint dst_ep of the
 * fourth node
 */
static void receive_from(uint16_t src_addr, uint8_t src_ep, uint16_t dst_addr, uint8_t dst_ep,
			 const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = dst_addr,
		.dst_ep = dst_ep,
		.src_addr = src_addr,
		.src_ep = src_ep,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_OTA,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(ota_node, buf, &ind);
}

/* Passes a frame of OTA Upgrade from the trust center to endpoint ep of the fourth node */
static void receive_ota(uint8_t ep, const uint8_t *asdu, size_t len)
{
	receive_from(REQUESTER_ADDR, REQUESTER_EP, NODE_ADDR, ep, asdu, len);
}

/*
 * Passes the command id with tsn, Disable Default Response 0 and the payload from src_addr to
 * endpoint ep after reset_frames(), and checks its Default Response
 */
static void command_from(uint16_t src_addr, uint8_t ep, uint8_t tsn, uint8_t id,
			 const uint8_t *payload, size_t len, uint8_t status)
{
	const uint8_t rsp[] = {0x10, tsn, 0x0b, id, status};
	uint8_t frame[32] = {0x09, tsn, id};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	reset_frames();
	receive_from(src_addr, REQUESTER_EP, NODE_ADDR, ep, frame, 3 + len);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_addr, src_addr);
	zassert_equal(sent[0].req.dst_ep, REQUESTER_EP);
	zassert_equal(sent[0].req.src_ep, ep);
	zassert_equal(sent[0].req.cluster, ZZB_ZCL_OTA);
	zassert_equal(sent[0].len, sizeof(rsp));
	zassert_mem_equal(sent[0].data, rsp, sizeof(rsp));
}

/* command_from() the trust center to endpoint 170 */
static void command(uint8_t tsn, uint8_t id, const uint8_t *payload, size_t len, uint8_t status)
{
	command_from(REQUESTER_ADDR, OTA_EP, tsn, id, payload, len, status);
}

/*
 * Checks that frame i is the command id of the client on endpoint 170 to endpoint server_ep of the
 * trust center, with Disable Default Response 0 and the payload, and returns its TSN
 */
static uint8_t check_request(int i, uint8_t server_ep, uint8_t id, const uint8_t *payload,
			     size_t len)
{
	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(sent[i].req.dst_mode, ZZB_APS_ADDR_SHORT);
	zassert_equal(sent[i].req.dst_addr, 0x0000);
	zassert_equal(sent[i].req.dst_ep, server_ep);
	zassert_equal(sent[i].req.src_ep, OTA_EP);
	zassert_equal(sent[i].req.profile, 0x0104);
	zassert_equal(sent[i].req.cluster, ZZB_ZCL_OTA);
	zassert_equal(sent[i].req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(sent[i].len, 3 + len);
	zassert_equal(sent[i].data[0], 0x01);
	zassert_equal(sent[i].data[2], id);
	zassert_mem_equal(sent[i].data + 3, payload, len);
	return sent[i].data[1];
}

/* Checks that frame i is Query Next Image Request to endpoint server_ep and returns its TSN */
static uint8_t check_query(int i, uint8_t server_ep)
{
	return check_request(i, server_ep, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE, query_req,
			     sizeof(query_req));
}

struct reboot_call {
	struct k_work work;
	int err;
};

static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_zcl_init(ota_node);
	call->err = zzb_settings_load(ota_node);
	if (call->err == 0) {
		zzb_zcl_start(ota_node);
	}
}

/* Starts ZCL of the fourth node again from its records, as reboot() */
static int reboot_ota(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void reset_handler(struct k_work *work)
{
	zzb_zcl_reset(ota_node);
}

/* Resets the ZCL attributes of the fourth node, as reset_node() */
static void reset_ota(void)
{
	struct k_work work;

	zzb_call_sync(&work, reset_handler);
}

/* Waits for the first query after a reboot and returns its TSN, then calls reset_frames() */
static uint8_t wait_query(void)
{
	uint8_t tsn;

	zassert_ok(reboot_ota());
	k_sleep(K_MSEC(INTERVAL_MS + 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	tsn = check_query(0, REQUESTER_EP);
	reset_frames();
	return tsn;
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = OTA_EUI64,
		.device = &ota_device,
	};

	test_node();
	zassert_ok(zzb_node_init(&cfg, &ota_node));
	return NULL;
}

/* Each test starts with the defaults */
static void before(void *fixture)
{
	reset_ota();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static void stop_handler(struct k_work *work)
{
	zzb_zcl_init(ota_node);
}

/* zzb_zcl_init() stops the queries of the fourth node before the other suites */
static void teardown(void *fixture)
{
	struct k_work work;

	zzb_call_sync(&work, stop_handler);
}

/*
 * ZCL Table 11-10: UpgradeServerID is apsTrustCenterAddress, ManufacturerID the manufacturer code
 * of the device, CurrentFileVersion that of the application, and the attributes of a download have
 * their defaults. The read goes to the client (direction 1), as zigpy sends it.
 */
ZTEST(zzb_zcl_ota, test_attributes)
{
	static const uint8_t read[] = {0x08, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00,
				       0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x06,
				       0x00, 0x07, 0x00, 0x08, 0x00, 0xfd, 0xff};
	static const uint8_t rsp[] = {
		0x10, 0x40, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x08, 0x07, 0x06, 0x05, 0x04, 0x4b, 0x12,
		0x00, 0x01, 0x00, 0x00, 0x23, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x23, 0x00,
		0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x21, 0x02, 0x00, 0x04, 0x00, 0x00, 0x23, 0xff,
		0xff, 0xff, 0xff, 0x06, 0x00, 0x00, 0x30, 0x00, 0x07, 0x00, 0x00, 0x21, 0x34, 0x12,
		0x08, 0x00, 0x00, 0x21, 0xff, 0xff, 0xfd, 0xff, 0x00, 0x21, 0x04, 0x00};

	receive_ota(OTA_EP, read, sizeof(read));
	check_frame(0, OTA_EP, ZZB_ZCL_OTA, rsp, sizeof(rsp));
}

/*
 * Section 16: nothing is sent at the start. The first Query Next Image Request goes to endpoint 1
 * of the trust center one interval after the start, and a sleepy node polls fast for the response.
 * The next query follows one interval later.
 */
ZTEST(zzb_zcl_ota, test_query)
{
	zassert_ok(reboot_ota());
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(INTERVAL_MS - 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_query(0, REQUESTER_EP);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 1);
	zassert_equal(zzb_nwk_poll_fast_fake.arg0_val, ota_node);

	k_sleep(K_MSEC(INTERVAL_MS - 200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	check_query(1, REQUESTER_EP);
	zassert_not_equal(sent[1].data[1], sent[0].data[1]);
}

/* Section 16: a query that APSDE-DATA refuses off a network is tried again after 60 s */
ZTEST(zzb_zcl_ota, test_query_refused)
{
	zassert_ok(reboot_ota());
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	k_sleep(K_MSEC(INTERVAL_MS + 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 0);

	reset_frames();
	k_sleep(K_MSEC(59800));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_query(0, REQUESTER_EP);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 1);
}

/* The reset of the node restarts the query interval */
ZTEST(zzb_zcl_ota, test_query_reset)
{
	zassert_ok(reboot_ota());
	k_sleep(K_MSEC(INTERVAL_MS / 2));
	reset_ota();
	k_sleep(K_MSEC(INTERVAL_MS - 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_query(0, REQUESTER_EP);
}

/*
 * ZCL 11.13.5.5.1: a SUCCESS shorter than 13 octets, with another manufacturer code or image type,
 * or with an image size below the 56 octets of the OTA header gets MALFORMED_COMMAND. A response
 * with another TSN is ignored, and NO_IMAGE_AVAILABLE and NOT_AUTHORIZED have only the status.
 */
ZTEST(zzb_zcl_ota, test_query_rsp_checks)
{
	static const uint8_t short_rsp[] = {0x00, 0x34, 0x12, 0x42, 0x00, 0x00,
					    0x00, 0x03, 0x01, 0x00, 0x01, 0x00};
	static const uint8_t other_manufacturer[] = {0x00, 0x35, 0x12, 0x42, 0x00, 0x00, 0x00,
						     0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	static const uint8_t other_type[] = {0x00, 0x34, 0x12, 0x43, 0x00, 0x00, 0x00,
					     0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	static const uint8_t no_header[] = {0x00, 0x34, 0x12, 0x42, 0x00, 0x00, 0x00,
					    0x03, 0x01, 0x37, 0x00, 0x00, 0x00};
	uint8_t tsn = wait_query();

	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, short_rsp, sizeof(short_rsp), 0x80);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, other_manufacturer,
		sizeof(other_manufacturer), 0x80);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, other_type, sizeof(other_type), 0x80);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, no_header, sizeof(no_header), 0x80);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, NULL, 0, 0x80);
	command(tsn + 1, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, short_rsp, sizeof(short_rsp), 0x00);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, (uint8_t[]){0x98}, 1, 0x00);
	command(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, (uint8_t[]){0x7e}, 1, 0x00);
}

/* Section 16: the server is the trust center, and commands from other nodes are ignored */
ZTEST(zzb_zcl_ota, test_other_source)
{
	static const uint8_t short_rsp[] = {0x00, 0x34, 0x12};
	uint8_t tsn = wait_query();

	command_from(OTHER_ADDR, OTA_EP, tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, short_rsp,
		     sizeof(short_rsp), 0x00);
}

/*
 * Section 16: the client on endpoint 171 has the attributes only, and Query Device Specific File
 * Response is not implemented
 */
ZTEST(zzb_zcl_ota, test_unsupported)
{
	command_from(REQUESTER_ADDR, SECOND_EP, 0x10, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP,
		     (uint8_t[]){0x98}, 1, 0x81);
	command(0x11, 0x09, (uint8_t[]){0x98}, 1, 0x81);
}

ZTEST_SUITE(zzb_zcl_ota, NULL, setup, before, after, teardown);
