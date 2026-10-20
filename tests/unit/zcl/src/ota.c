/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
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

/* Calls of ota_cb */
static int ota_cb_calls;

static void record_ota_cb(struct zzb_node *node)
{
	zassert_equal(node, ota_node);
	ota_cb_calls++;
}

/* The query interval in milliseconds */
#define INTERVAL_MS (CONFIG_ZZB_ZCL_OTA_QUERY_INTERVAL * 60000LL)

/* A node other than the trust center, whose commands the client ignores */
#define OTHER_ADDR 0x1234

/* The file version of the image that the server offers */
#define NEW_VERSION 0x01020400

#define BLOCK CONFIG_ZZB_ZCL_OTA_BLOCK_SIZE

/* The OTA header and the sub-element header (ZCL 11.4.2, 11.4.3) */
#define HEADER_LEN  56
#define ELEMENT_LEN 6

/* An MCUboot image: a header of 32 octets and a pattern */
#define IMAGE_LEN 150

/* The OTA file that the server offers, built by the test, and the image size of the offer */
static uint8_t file[512];
static size_t file_len;
static uint32_t image_size;

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
	uint8_t frame[96] = {0x09, tsn, id};

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

/* Passes Image Notify with the payload from src_addr and src_ep to dst_addr and endpoint dst_ep */
static void notify(uint16_t src_addr, uint8_t src_ep, uint16_t dst_addr, uint8_t dst_ep,
		   const uint8_t *payload, size_t len)
{
	uint8_t frame[16] = {0x19, 0x20, ZZB_ZCL_OTA_IMAGE_NOTIFY};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	receive_from(src_addr, src_ep, dst_addr, dst_ep, frame, 3 + len);
}

/* Passes Image Notify with the payload from endpoint 1 of the trust center as a broadcast */
static void broadcast_notify(const uint8_t *payload, size_t len)
{
	notify(REQUESTER_ADDR, REQUESTER_EP, 0xffff, OTA_EP, payload, len);
}

/* Waits for the first query after a reboot and returns its TSN, then calls reset_frames() */
static uint8_t wait_query(void)
{
	uint8_t tsn;

	zassert_ok(reboot_ota());
	reset_frames();
	k_sleep(K_MSEC(INTERVAL_MS + 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	tsn = check_query(0, REQUESTER_EP);
	reset_frames();
	return tsn;
}

/*
 * Starts an OTA file (ZCL 11.4.2) with header version 0x0100, header length hdr_len and the
 * manufacturer code, image type and file version of the offer
 */
static void start_file(uint16_t hdr_len)
{
	memset(file, 0, sizeof(file));
	sys_put_le32(0x0beef11e, file);
	sys_put_le16(0x0100, file + 4);
	sys_put_le16(hdr_len, file + 6);
	sys_put_le16(OTA_MANUFACTURER, file + 10);
	sys_put_le16(OTA_IMAGE_TYPE, file + 12);
	sys_put_le32(NEW_VERSION, file + 14);
	sys_put_le16(0x0002, file + 18);
	file_len = hdr_len;
}

/* Appends a sub-element (ZCL 11.4.3) with tag and length len and data_len octets of data */
static void add_element(uint16_t tag, uint32_t len, const uint8_t *data, size_t data_len)
{
	zassert_true(file_len + ELEMENT_LEN + data_len <= sizeof(file));
	sys_put_le16(tag, file + file_len);
	sys_put_le32(len, file + file_len + 2);
	memcpy(file + file_len + ELEMENT_LEN, data, data_len);
	file_len += ELEMENT_LEN + data_len;
}

/* Writes the length of the file as the total image size of the OTA header and of the offer */
static void finish_file(void)
{
	image_size = file_len;
	sys_put_le32(image_size, file + 52);
}

/*
 * An MCUboot image: the magic 0x96f3b83d, header size 32 and image size of the MCUboot image
 * header, then a pattern
 */
static void build_image(uint8_t *image)
{
	memset(image, 0, IMAGE_LEN);
	sys_put_le32(0x96f3b83d, image);
	sys_put_le16(32, image + 8);
	sys_put_le32(IMAGE_LEN - 32, image + 12);
	for (int i = 32; i < IMAGE_LEN; i++) {
		image[i] = i;
	}
}

/* An OTA file with the OTA header and an Upgrade Image sub-element with the MCUboot image */
static void build_file(void)
{
	uint8_t image[IMAGE_LEN];

	build_image(image);
	start_file(HEADER_LEN);
	add_element(0x0000, IMAGE_LEN, image, IMAGE_LEN);
	finish_file();
}

/* Passes the command id with tsn and the payload from the trust center to endpoint 170, as zigpy */
static void server_cmd(uint8_t tsn, uint8_t id, const uint8_t *payload, size_t len)
{
	uint8_t frame[96] = {0x19, tsn, id};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	receive_ota(OTA_EP, frame, 3 + len);
}

/* Answers the query with tsn with SUCCESS and the file after reset_frames() */
static void offer(uint8_t tsn)
{
	uint8_t payload[13] = {0x00, 0x34, 0x12, 0x42, 0x00};

	sys_put_le32(NEW_VERSION, payload + 5);
	sys_put_le32(image_size, payload + 9);
	reset_frames();
	server_cmd(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, payload, sizeof(payload));
}

/* Checks that frame i is Image Block Request at offset and returns its TSN */
static uint8_t check_block_req(int i, uint32_t offset)
{
	uint8_t req[14] = {0x00, 0x34, 0x12, 0x42, 0x00};

	sys_put_le32(NEW_VERSION, req + 5);
	sys_put_le32(offset, req + 9);
	req[13] = BLOCK;
	return check_request(i, REQUESTER_EP, ZZB_ZCL_OTA_IMAGE_BLOCK, req, sizeof(req));
}

/* Answers the request with tsn with len octets of the file at offset after reset_frames() */
static void block_rsp(uint8_t tsn, uint32_t offset, uint8_t len)
{
	uint8_t payload[14 + BLOCK] = {0x00, 0x34, 0x12, 0x42, 0x00};

	sys_put_le32(NEW_VERSION, payload + 5);
	sys_put_le32(offset, payload + 9);
	payload[13] = len;
	memcpy(payload + 14, file + offset, len);
	reset_frames();
	server_cmd(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, payload, 14 + len);
}

/* Answers the block requests of the client with the blocks of the file before offset end */
static void serve(uint32_t end)
{
	for (uint32_t offset = ota170.file_offset; offset < end; offset += BLOCK) {
		zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
		block_rsp(check_block_req(0, offset), offset, MIN(BLOCK, file_len - offset));
	}
}

/* Checks that frame i is Upgrade End Request with status */
static void check_end_req(int i, uint8_t status)
{
	uint8_t req[9] = {status, 0x34, 0x12, 0x42, 0x00};

	sys_put_le32(NEW_VERSION, req + 5);
	check_request(i, REQUESTER_EP, ZZB_ZCL_OTA_UPGRADE_END, req, sizeof(req));
}

/* The last hold of the poll rate by the OTA Upgrade client */
static bool held(void)
{
	zassert_true(zzb_nwk_poll_hold_fake.call_count <= FFF_ARG_HISTORY_LEN);
	for (int i = zzb_nwk_poll_hold_fake.call_count - 1; i >= 0; i--) {
		if (zzb_nwk_poll_hold_fake.arg1_history[i] == ZZB_NWK_HOLD_OTA) {
			zassert_equal(zzb_nwk_poll_hold_fake.arg0_history[i], ota_node);
			return zzb_nwk_poll_hold_fake.arg2_history[i];
		}
	}

	zassert_unreachable("no hold");
	return false;
}

/* Checks that the upload slot starts with image */
static void check_slot(const uint8_t *image)
{
	const struct flash_area *fa;
	uint8_t data[IMAGE_LEN];

	zassert_ok(flash_area_open(PARTITION_ID(slot1_partition), &fa));
	zassert_ok(flash_area_read(fa, 0, data, sizeof(data)));
	flash_area_close(fa);
	zassert_mem_equal(data, image, IMAGE_LEN);
}

/* Downloads the file up to Upgrade End Request SUCCESS, then calls reset_frames() */
static void download(void)
{
	offer(wait_query());
	serve(file_len);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_end_req(0, 0x00);
	reset_frames();
}

/* Writes the payload of Upgrade End Response for version with CurrentTime now and UpgradeTime at */
static void put_end_rsp(uint8_t *payload, uint32_t version, uint32_t now, uint32_t at)
{
	sys_put_le16(OTA_MANUFACTURER, payload);
	sys_put_le16(OTA_IMAGE_TYPE, payload + 2);
	sys_put_le32(version, payload + 4);
	sys_put_le32(now, payload + 8);
	sys_put_le32(at, payload + 12);
}

/* Passes Upgrade End Response with CurrentTime now and UpgradeTime at, as zigpy */
static void end_rsp(uint32_t now, uint32_t at)
{
	uint8_t payload[16];

	put_end_rsp(payload, NEW_VERSION, now, at);
	server_cmd(0x30, ZZB_ZCL_OTA_UPGRADE_END_RSP, payload, sizeof(payload));
}

/* The defaults outside a download (ZCL Table 11-10) */
static void check_normal(void)
{
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_NORMAL);
	zassert_equal(ota170.file_offset, 0xffffffff);
	zassert_equal(ota170.downloaded_file_version, 0xffffffff);
	zassert_equal(ota170.image_type_id, 0xffff);
}

static void *setup(void)
{
	const struct zzb_node_config cfg = {
		.eui64 = OTA_EUI64,
		.device = &ota_device,
		.zcl_cb = record_zcl_cb,
		.ota_cb = record_ota_cb,
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
	build_file();
	zassert_ok(boot_erase_img_bank(PARTITION_ID(slot1_partition)));
	ota_cb_calls = 0;
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

/*
 * ZCL 11.13.3.4: a unicast Image Notify of the trust center starts a query at once, to the endpoint
 * of the notify, and restarts the interval. An Image Notify of another node is ignored. The reset
 * of the node sends the queries to endpoint 1 again.
 */
ZTEST(zzb_zcl_ota, test_notify_unicast)
{
	zassert_ok(reboot_ota());
	k_sleep(K_MSEC(INTERVAL_MS / 2));
	notify(OTHER_ADDR, REQUESTER_EP, NODE_ADDR, OTA_EP, (uint8_t[]){0x00, 0x64}, 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	notify(REQUESTER_ADDR, 5, NODE_ADDR, OTA_EP, (uint8_t[]){0x00, 0x64}, 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_query(0, 5);
	zassert_equal(zzb_nwk_poll_fast_fake.call_count, 1);

	k_sleep(K_MSEC(INTERVAL_MS - 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	check_query(1, 5);

	reset_ota();
	k_sleep(K_MSEC(INTERVAL_MS + 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	check_query(2, REQUESTER_EP);
}

/*
 * ZCL 11.13.3.4, 11.13.3.5.1: a broadcast Image Notify is ignored when it is malformed, when its
 * manufacturer code or image type is another one, or when it offers CurrentFileVersion. Otherwise
 * QueryJitter 100 starts a query, also to the broadcast endpoint and with the wildcards.
 */
ZTEST(zzb_zcl_ota, test_notify_broadcast)
{
	static const uint8_t current[] = {0x03, 0x64, 0x34, 0x12, 0x42,
					  0x00, 0x00, 0x03, 0x02, 0x01};
	static const uint8_t short_type3[] = {0x03, 0x64, 0x34, 0x12, 0x42, 0x00, 0x00, 0x04, 0x02};
	static const uint8_t newer[] = {0x03, 0x64, 0x34, 0x12, 0x42, 0x00, 0x00, 0x04, 0x02, 0x01};
	static const uint8_t wildcards[] = {0x03, 0x64, 0xff, 0xff, 0xff,
					    0xff, 0xff, 0xff, 0xff, 0xff};

	zassert_ok(reboot_ota());
	broadcast_notify((uint8_t[]){0x04, 0x64}, 2);
	broadcast_notify((uint8_t[]){0x00, 0x00}, 2);
	broadcast_notify((uint8_t[]){0x00, 0x65}, 2);
	broadcast_notify((uint8_t[]){0x00}, 1);
	broadcast_notify((uint8_t[]){0x01, 0x64, 0x34}, 3);
	broadcast_notify(short_type3, sizeof(short_type3));
	broadcast_notify((uint8_t[]){0x01, 0x64, 0x35, 0x12}, 4);
	broadcast_notify((uint8_t[]){0x02, 0x64, 0xff, 0xff, 0x43, 0x00}, 6);
	broadcast_notify(current, sizeof(current));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);

	broadcast_notify((uint8_t[]){0x00, 0x64}, 2);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_query(0, REQUESTER_EP);
	broadcast_notify((uint8_t[]){0x02, 0x64, 0x34, 0x12, 0x42, 0x00}, 6);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	broadcast_notify(newer, sizeof(newer));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	notify(REQUESTER_ADDR, REQUESTER_EP, NODE_ADDR, 0xff, wildcards, sizeof(wildcards));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	check_query(3, REQUESTER_EP);
}

/*
 * ZCL 11.13.5.4: NO_IMAGE_AVAILABLE, NOT_AUTHORIZED and a SUCCESS with CurrentFileVersion end the
 * query without a download
 */
ZTEST(zzb_zcl_ota, test_no_download)
{
	uint8_t current[13] = {0x00, 0x34, 0x12, 0x42, 0x00, 0x00, 0x03, 0x02, 0x01};
	uint8_t tsn = wait_query();

	sys_put_le32(file_len, current + 9);
	server_cmd(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, (uint8_t[]){0x98}, 1);
	server_cmd(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, (uint8_t[]){0x7e}, 1);
	server_cmd(tsn, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP, current, sizeof(current));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	check_normal();
}

/*
 * ZCL 11.13.5.4, 11.13.6, 11.13.8.4: a SUCCESS starts the download with zcl_cb, the client asks for
 * the blocks from offset 0 and holds the fast poll rate while it waits for them, FileOffset follows
 * the blocks, and after the last block the download is complete and the client waits for the
 * response to Upgrade End Request SUCCESS
 */
ZTEST(zzb_zcl_ota, test_download)
{
	static const uint16_t start_attrs[] = {0x0006, 0x0008, 0x0004, 0x0001};
	static const uint16_t blocks_attrs[] = {0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0006};
	uint8_t tsn = wait_query();

	zcl_cb_calls = (struct zcl_cb_calls){0};
	offer(tsn);
	check_zcl_cb(OTA_EP, ZZB_ZCL_OTA, start_attrs, ARRAY_SIZE(start_attrs));
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOADING);
	zassert_equal(ota170.image_type_id, OTA_IMAGE_TYPE);
	zassert_equal(ota170.downloaded_file_version, NEW_VERSION);
	zassert_equal(ota170.file_offset, 0);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_block_req(0, 0);
	zassert_true(held());

	zcl_cb_calls = (struct zcl_cb_calls){0};
	tsn = check_block_req(0, 0);
	block_rsp(tsn, 0, BLOCK);
	zassert_equal(ota170.file_offset, BLOCK);
	check_block_req(0, BLOCK);
	zassert_not_equal(sent[0].data[1], tsn);
	zassert_true(held());
	serve(file_len);
	zassert_equal(ota170.file_offset, file_len);
	check_zcl_cb(OTA_EP, ZZB_ZCL_OTA, blocks_attrs, ARRAY_SIZE(blocks_attrs));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_end_req(0, 0x00);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
	zassert_true(held());
	check_slot(file + HEADER_LEN + ELEMENT_LEN);
}

/*
 * ZCL 11.13.8.5.1: an Image Block Response SUCCESS that does not match the request gets
 * MALFORMED_COMMAND and is ignored, and so does a WAIT_FOR_DATA shorter than 9 octets or with a
 * RequestTime below CurrentTime. A response with another TSN is ignored.
 */
ZTEST(zzb_zcl_ota, test_block_rsp_checks)
{
	uint8_t rsp[16 + BLOCK] = {0x00, 0x34, 0x12, 0x42, 0x00};
	uint8_t tsn;

	offer(wait_query());
	serve(4 * BLOCK);
	tsn = check_block_req(0, 4 * BLOCK);
	sys_put_le32(NEW_VERSION, rsp + 5);
	sys_put_le32(4 * BLOCK, rsp + 9);
	rsp[13] = 1;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 13, 0x80);
	rsp[1] = 0x35;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 15, 0x80);
	rsp[1] = 0x34;
	rsp[3] = 0x43;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 15, 0x80);
	rsp[3] = 0x42;
	rsp[5] = 0x01;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 15, 0x80);
	rsp[5] = 0x00;
	rsp[9] = 0;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 15, 0x80);
	sys_put_le32(4 * BLOCK, rsp + 9);
	rsp[13] = 0;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 14, 0x80);
	rsp[13] = 2;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 15, 0x80);
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 17, 0x80);
	rsp[13] = file_len - 4 * BLOCK + 1;
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 14 + rsp[13], 0x80);
	command(tsn + 1, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, rsp, 13, 0x00);
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, (uint8_t[]){0x97, 0x00, 0x00, 0x00, 0x00}, 5,
		0x80);
	command(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP,
		(uint8_t[]){0x97, 0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00}, 9, 0x80);
	zassert_equal(ota170.file_offset, 4 * BLOCK);
	zassert_true(held());

	reset_frames();
	block_rsp(tsn, 4 * BLOCK, file_len - 4 * BLOCK);
	check_end_req(0, 0x00);
}

/*
 * Section 16: a block request without a response is sent again after 5 s, and the download ends
 * without a frame after three repetitions. The next query follows one interval later.
 */
ZTEST(zzb_zcl_ota, test_block_retries)
{
	static const uint16_t end_attrs[] = {0x0006, 0x0001, 0x0004, 0x0008};

	offer(wait_query());
	serve(BLOCK);
	check_block_req(0, BLOCK);
	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 2);
	check_block_req(1, BLOCK);
	zassert_true(held());
	k_sleep(K_SECONDS(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	check_block_req(3, BLOCK);
	k_sleep(K_MSEC(4800));
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOADING);

	zcl_cb_calls = (struct zcl_cb_calls){0};
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	check_normal();
	check_zcl_cb(OTA_EP, ZZB_ZCL_OTA, end_attrs, ARRAY_SIZE(end_attrs));
	zassert_false(held());

	k_sleep(K_MSEC(INTERVAL_MS - 200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	k_sleep(K_MSEC(200));
	check_query(4, REQUESTER_EP);
}

/*
 * ZCL 11.13.8.2.8: WAIT_FOR_DATA sends the same request again after RequestTime seconds when
 * CurrentTime is 0, otherwise after RequestTime - CurrentTime seconds. The client does not hold the
 * fast poll rate meanwhile.
 */
ZTEST(zzb_zcl_ota, test_wait_for_data)
{
	static const uint8_t wait_offset[] = {0x97, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00};
	static const uint8_t wait_utc[] = {0x97, 0x00, 0x00, 0x00, 0x20, 0x0a,
					   0x00, 0x00, 0x20, 0x00, 0x00};
	uint8_t tsn;

	offer(wait_query());
	tsn = check_block_req(0, 0);
	reset_frames();
	server_cmd(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, wait_offset, sizeof(wait_offset));
	zassert_false(held());
	k_sleep(K_MSEC(29900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	tsn = check_block_req(0, 0);
	zassert_true(held());

	reset_frames();
	server_cmd(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, wait_utc, sizeof(wait_utc));
	k_sleep(K_MSEC(9900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_block_req(0, 0);
}

/* ZCL 11.13.8.4: ABORT ends the download without a frame */
ZTEST(zzb_zcl_ota, test_abort)
{
	uint8_t tsn;

	offer(wait_query());
	serve(BLOCK);
	tsn = check_block_req(0, BLOCK);
	reset_frames();
	server_cmd(tsn, ZZB_ZCL_OTA_IMAGE_BLOCK_RSP, (uint8_t[]){0x95}, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	check_normal();
	zassert_false(held());

	k_sleep(K_MSEC(INTERVAL_MS - 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	check_query(0, REQUESTER_EP);
}

/*
 * Section 16: zzb_zcl_init() ends a download without a frame and clears the hold, and the client
 * downloads again from offset 0
 */
ZTEST(zzb_zcl_ota, test_reboot)
{
	offer(wait_query());
	serve(BLOCK);
	zassert_ok(reboot_ota());
	zassert_false(held());
	check_normal();

	reset_frames();
	notify(REQUESTER_ADDR, REQUESTER_EP, NODE_ADDR, OTA_EP, (uint8_t[]){0x00, 0x64}, 2);
	offer(check_query(0, REQUESTER_EP));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_block_req(0, 0);
}

/*
 * ZCL 11.4.2, 11.4.3: the client skips the OTA header up to its header length and the sub-elements
 * other than the Upgrade Image, and writes the Upgrade Image to the upload slot
 */
ZTEST(zzb_zcl_ota, test_upgrade_image)
{
	static const uint8_t other[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	uint8_t image[IMAGE_LEN];

	build_image(image);
	start_file(HEADER_LEN + 6);
	add_element(0xf000, sizeof(other), other, sizeof(other));
	add_element(0x0000, IMAGE_LEN, image, IMAGE_LEN);
	add_element(0x0003, 8, other, 8);
	finish_file();
	offer(wait_query());
	serve(file_len);
	check_end_req(0, 0x00);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
	check_slot(image);
}

/*
 * Offers the file and serves its blocks before offset end, after which the download ends with
 * Upgrade End Request INVALID_IMAGE
 */
static void check_invalid(uint32_t end)
{
	offer(wait_query());
	serve(end);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_end_req(0, 0x96);
	check_normal();
	zassert_false(held());
}

/*
 * Section 16: an OTA header with another identifier or total image size, a second Upgrade Image, an
 * Upgrade Image longer than the slot and a sub-element that passes the end of the file give
 * INVALID_IMAGE at once, and an upload slot without an MCUboot image header after the last block
 */
ZTEST(zzb_zcl_ota, test_invalid_image)
{
	uint8_t image[IMAGE_LEN];

	file[0] = 0x1f;
	check_invalid(2 * BLOCK);

	build_file();
	sys_put_le32(image_size + 1, file + 52);
	check_invalid(2 * BLOCK);

	build_image(image);
	build_file();
	add_element(0x0000, 10, image, 10);
	finish_file();
	check_invalid(file_len);

	start_file(HEADER_LEN);
	add_element(0x0000, 0x69001, image, 2 * BLOCK - HEADER_LEN - ELEMENT_LEN);
	image_size = HEADER_LEN + ELEMENT_LEN + 0x69001;
	sys_put_le32(image_size, file + 52);
	check_invalid(2 * BLOCK);

	start_file(HEADER_LEN);
	add_element(0x0000, IMAGE_LEN + 1, image, IMAGE_LEN);
	finish_file();
	check_invalid(2 * BLOCK);

	image[0] = 0;
	start_file(HEADER_LEN);
	add_element(0x0000, IMAGE_LEN, image, IMAGE_LEN);
	finish_file();
	check_invalid(file_len);
}

/*
 * Section 16: Upgrade End Response with UpgradeTime 0, as zigpy sends it, requests the test upgrade
 * of MCUboot at once and calls ota_cb. The client stays in Count down, ignores the responses and
 * sends nothing more.
 */
ZTEST(zzb_zcl_ota, test_upgrade)
{
	download();
	zassert_true(held());
	end_rsp(0, 0);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_COUNT_DOWN);
	k_sleep(K_MSEC(10));
	zassert_equal(ota_cb_calls, 1);
	zassert_equal(mcuboot_swap_type(), BOOT_SWAP_TYPE_TEST);
	zassert_false(held());

	end_rsp(0, 0);
	k_sleep(K_MINUTES(61));
	zassert_equal(ota_cb_calls, 1);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_COUNT_DOWN);
}

/*
 * ZCL 11.15.4: the upgrade follows UpgradeTime seconds after the response when CurrentTime is 0,
 * otherwise UpgradeTime - CurrentTime seconds after it. An UpgradeTime below CurrentTime gets
 * MALFORMED_COMMAND.
 */
ZTEST(zzb_zcl_ota, test_count_down)
{
	uint8_t early[16];

	download();
	end_rsp(0, 30);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_COUNT_DOWN);
	zassert_false(held());
	k_sleep(K_MSEC(29900));
	zassert_equal(ota_cb_calls, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(ota_cb_calls, 1);
	zassert_equal(mcuboot_swap_type(), BOOT_SWAP_TYPE_TEST);

	zassert_ok(boot_erase_img_bank(PARTITION_ID(slot1_partition)));
	download();
	put_end_rsp(early, NEW_VERSION, 0x20000010, 0x2000000f);
	command(0x31, ZZB_ZCL_OTA_UPGRADE_END_RSP, early, sizeof(early), 0x80);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
	end_rsp(0x20000000, 0x2000001e);
	k_sleep(K_MSEC(29900));
	zassert_equal(ota_cb_calls, 1);
	k_sleep(K_MSEC(200));
	zassert_equal(ota_cb_calls, 2);
}

/*
 * ZCL 11.16: UpgradeTime 0xffffffff makes the client wait and send Upgrade End Request again every
 * 60 minutes, and an unsolicited Upgrade End Response with the wildcards starts the upgrade
 */
ZTEST(zzb_zcl_ota, test_upgrade_waiting)
{
	static const uint8_t wildcards[] = {0x19, 0x31, 0x07, 0xff, 0xff, 0xff, 0xff,
					    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x00, 0x00};

	download();
	end_rsp(0, 0xffffffff);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_WAITING);
	zassert_false(held());
	k_sleep(K_MSEC(60 * 60000 - 100));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_end_req(0, 0x00);
	zassert_true(held());

	receive_from(REQUESTER_ADDR, REQUESTER_EP, 0xffff, 0xff, wildcards, sizeof(wildcards));
	k_sleep(K_MSEC(10));
	zassert_equal(ota_cb_calls, 1);
	zassert_equal(mcuboot_swap_type(), BOOT_SWAP_TYPE_TEST);
}

/*
 * Section 16: an Upgrade End Request without a response is sent again after 5 s, and after three
 * repetitions the client waits as for UpgradeTime 0xffffffff
 */
ZTEST(zzb_zcl_ota, test_end_req_retries)
{
	download();
	k_sleep(K_MSEC(4900));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	k_sleep(K_MSEC(200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_end_req(0, 0x00);
	k_sleep(K_SECONDS(10));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	k_sleep(K_MSEC(4800));
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
	zassert_true(held());
	k_sleep(K_MSEC(200));
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_WAITING);
	zassert_false(held());

	k_sleep(K_MSEC(60 * 60000 - 200));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 3);
	k_sleep(K_MSEC(400));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 4);
	check_end_req(3, 0x00);
}

/*
 * Section 16: Upgrade End Response is ignored before the download is complete and with another
 * file version or manufacturer code, and one shorter than 16 octets gets MALFORMED_COMMAND
 */
ZTEST(zzb_zcl_ota, test_end_rsp_ignored)
{
	uint8_t rsp[16];

	put_end_rsp(rsp, NEW_VERSION, 0, 0);
	command(0x40, ZZB_ZCL_OTA_UPGRADE_END_RSP, rsp, sizeof(rsp), 0x00);
	offer(wait_query());
	command(0x41, ZZB_ZCL_OTA_UPGRADE_END_RSP, rsp, sizeof(rsp), 0x00);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOADING);

	download();
	command(0x42, ZZB_ZCL_OTA_UPGRADE_END_RSP, rsp, 15, 0x80);
	put_end_rsp(rsp, NEW_VERSION + 1, 0, 0);
	command(0x43, ZZB_ZCL_OTA_UPGRADE_END_RSP, rsp, sizeof(rsp), 0x00);
	put_end_rsp(rsp, NEW_VERSION, 0, 0);
	rsp[0] = 0x35;
	command(0x44, ZZB_ZCL_OTA_UPGRADE_END_RSP, rsp, sizeof(rsp), 0x00);
	k_sleep(K_MSEC(10));
	zassert_equal(ota_cb_calls, 0);
	zassert_equal(ota170.image_upgrade_status, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
}

/*
 * Section 16: the reset of the node ends a download without a frame and restores the defaults with
 * zcl_cb, and the client downloads again from offset 0
 */
ZTEST(zzb_zcl_ota, test_reset_download)
{
	static const uint16_t end_attrs[] = {0x0006, 0x0001, 0x0004, 0x0008};

	offer(wait_query());
	serve(BLOCK);
	reset_frames();
	zcl_cb_calls = (struct zcl_cb_calls){0};
	reset_ota();
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
	check_normal();
	check_zcl_cb(OTA_EP, ZZB_ZCL_OTA, end_attrs, ARRAY_SIZE(end_attrs));
	zassert_false(held());

	notify(REQUESTER_ADDR, REQUESTER_EP, NODE_ADDR, OTA_EP, (uint8_t[]){0x00, 0x64}, 2);
	offer(check_query(0, REQUESTER_EP));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	check_block_req(0, 0);
}

ZTEST_SUITE(zzb_zcl_ota, NULL, setup, before, after, teardown);
