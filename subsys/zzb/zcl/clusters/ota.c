/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zzb/zcl/ota.h>

#include "aps/aps.h"
#include "common/node.h"
#include "common/queue.h"
#include "nwk/nwk.h"
#include "zcl/zcl.h"

/* ClusterRevision (ZCL 11.3.1.1) */
#define REVISION 4

/* The server is the trust center, on endpoint 1 before the first Image Notify */
#define SERVER    0x0000
#define SERVER_EP 1

#define INTERVAL K_MINUTES(CONFIG_ZZB_ZCL_OTA_QUERY_INTERVAL)

/* The next query after one that APSDE-DATA refuses */
#define RETRY K_SECONDS(60)

/* A request without a response is sent again after 5 s, three times */
#define TIMEOUT K_SECONDS(5)
#define REPEATS 3

/* While the client waits for the upgrade time, it asks for it every 60 minutes (ZCL 11.16) */
#define WAIT K_MINUTES(60)

/* The upgrade waits at least this long, so the APS ACK of Upgrade End Response goes out first */
#define UPGRADE_MIN_MS 1000

/* The OTA header and the header of a sub-element (ZCL 11.4.2, 11.4.3) */
#define HEADER_LEN  56
#define ELEMENT_LEN 6
#define FILE_ID     0x0beef11e

/* The tag of the Upgrade Image sub-element (ZCL Table 11-5) */
#define UPGRADE_IMAGE 0x0000

/*
 * The download into the upload slot, which one node of the binary owns at a time (section 3), and
 * the parser of the OTA file
 */
struct download {
	/* NULL while no node downloads */
	struct zzb_node *owner;
	/* The image size of the Query Next Image Response */
	uint32_t size;
	struct flash_img_context flash;
	/* The OTA header, then the header of each sub-element, as it arrives */
	uint8_t hdr[HEADER_LEN];
	uint8_t hdr_len;
	uint8_t hdr_got;
	/* Octets of the header or sub-element after hdr that are still to come */
	uint32_t left;
	/* Whether those octets go to the upload slot */
	bool writing;
	/* Whether the file has an Upgrade Image */
	bool image;
};

static struct download dl;

static const struct zzb_zcl_attr attrs[] = {
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_UPGRADE_SERVER_ID, ZZB_ZCL_EUI64, 0, struct zzb_zcl_ota,
		     upgrade_server_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_FILE_OFFSET, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota, file_offset),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_CURRENT_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     current_file_version),
	ZZB_ZCL_ATTR_CONST(ZZB_ZCL_OTA_CURRENT_STACK_VERSION, ZZB_ZCL_UINT16,
			   ZZB_ZCL_OTA_STACK_VERSION_PRO),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION, ZZB_ZCL_UINT32, 0, struct zzb_zcl_ota,
		     downloaded_file_version),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_ENUM8, 0, struct zzb_zcl_ota,
		     image_upgrade_status),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_MANUFACTURER_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     manufacturer_id),
	ZZB_ZCL_ATTR(ZZB_ZCL_OTA_IMAGE_TYPE_ID, ZZB_ZCL_UINT16, 0, struct zzb_zcl_ota,
		     image_type_id),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, REVISION),
};

/* The defaults outside a download (ZCL Table 11-10); the application sets the running image */
static void init(void *values)
{
	struct zzb_zcl_ota *v = values;

	v->file_offset = ZZB_ZCL_OTA_FILE_OFFSET_NONE;
	v->downloaded_file_version = ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION_NONE;
	v->image_upgrade_status = ZZB_ZCL_OTA_NORMAL;
	v->image_type_id = ZZB_ZCL_OTA_IMAGE_TYPE_ID_NONE;
	v->server_ep = SERVER_EP;
	v->tries = 0;
}

static void refresh(struct zzb_node *node, uint8_t ep, void *values)
{
	struct zzb_zcl_ota *v = values;

	v->upgrade_server_id = zzb_apsme_get_tc_addr(node);
	v->manufacturer_id = node->device->manufacturer_code;
}

/* The first endpoint with an OTA client, which downloads */
static const struct zzb_endpoint *first(const struct zzb_node *node)
{
	for (size_t i = 0; i < node->device->endpoint_count; i++) {
		const struct zzb_endpoint *ep = &node->device->endpoints[i];

		if (zzb_zcl_find_cluster(ep, ZZB_ZCL_OTA, false) != NULL) {
			return ep;
		}
	}

	return NULL;
}

static struct zzb_zcl_ota *values_of(const struct zzb_endpoint *ep)
{
	return zzb_zcl_find_cluster(ep, ZZB_ZCL_OTA, false)->values;
}

/* Writes value to attribute attr of the client on ep and reports a change with zcl_cb */
static void set(struct zzb_node *node, const struct zzb_endpoint *ep, uint16_t attr, uint32_t value)
{
	const struct zzb_zcl_cluster *cl = zzb_zcl_find_cluster(ep, ZZB_ZCL_OTA, false);

	if (zzb_zcl_attr_write(cl, zzb_zcl_attr_find(cl, attr), value)) {
		zzb_zcl_changed(node, ep->id, cl, attr);
	}
}

/* Arms the timer and holds the fast poll rate while the client waits for a response */
static void arm(struct zzb_node *node, const struct zzb_zcl_ota *v, k_timeout_t delay)
{
	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_OTA, v->tries != 0);
	k_work_reschedule_for_queue(&zzb_workq, &node->zcl.ota_timer, delay);
}

/* Writes the manufacturer code of the device, the image type and the file version of a payload */
static void put_image(uint8_t *p, const struct zzb_node *node, uint16_t type, uint32_t version)
{
	sys_put_le16(node->device->manufacturer_code, p);
	sys_put_le16(type, p + 2);
	sys_put_le32(version, p + 4);
}

static void release(const struct zzb_node *node)
{
	if (dl.owner == node) {
		dl.owner = NULL;
	}
}

/* Sends the command id with the payload from the client on ep to the server */
static int request(struct zzb_node *node, const struct zzb_endpoint *ep, uint8_t id,
		   const uint8_t *payload, size_t len)
{
	struct zzb_zcl_ota *v = values_of(ep);
	const struct zzb_zcl_hdr hdr = {
		.type = ZZB_ZCL_FRAME_CLUSTER,
		.tsn = node->zcl.tsn++,
		.cmd = id,
	};
	const struct zzb_apsde_req aps = {
		.dst_mode = ZZB_APS_ADDR_SHORT,
		.dst_addr = SERVER,
		.dst_ep = v->server_ep,
		.profile = ep->profile,
		.cluster = ZZB_ZCL_OTA,
		.src_ep = ep->id,
		.tx_options = ZZB_APS_TX_ACK,
	};

	v->tsn = hdr.tsn;
	return zzb_zcl_send(node, &aps, &hdr, payload, len);
}

/* Query Next Image Request (ZCL 11.13.4); a sleepy node polls fast for the response */
static void query(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);
	uint8_t payload[9] = {0};

	put_image(payload + 1, node, v->image_type, v->current_file_version);
	if (request(node, ep, ZZB_ZCL_OTA_QUERY_NEXT_IMAGE, payload, sizeof(payload)) != 0) {
		arm(node, v, RETRY);
		return;
	}

	zzb_nwk_poll_fast(node);
	arm(node, v, INTERVAL);
}

/* Image Block Request (ZCL 11.13.6) at FileOffset */
static void block_req(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);
	uint8_t payload[14] = {0};

	put_image(payload + 1, node, v->image_type_id, v->downloaded_file_version);
	sys_put_le32(v->file_offset, payload + 9);
	payload[13] = CONFIG_ZZB_ZCL_OTA_BLOCK_SIZE;
	request(node, ep, ZZB_ZCL_OTA_IMAGE_BLOCK, payload, sizeof(payload));
}

/* Upgrade End Request (ZCL 11.13.9) with status */
static void end_req(struct zzb_node *node, const struct zzb_endpoint *ep, uint8_t status)
{
	struct zzb_zcl_ota *v = values_of(ep);
	uint8_t payload[9] = {status};

	put_image(payload + 1, node, v->image_type_id, v->downloaded_file_version);
	request(node, ep, ZZB_ZCL_OTA_UPGRADE_END, payload, sizeof(payload));
}

/*
 * Sends Image Block Request during the download and Upgrade End Request SUCCESS after it, and waits
 * for the response. A request that APSDE-DATA refuses counts as one without a response.
 */
static void await_rsp(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);

	if (v->image_upgrade_status == ZZB_ZCL_OTA_DOWNLOADING) {
		block_req(node, ep);
	} else {
		end_req(node, ep, ZZB_ZCL_SUCCESS);
	}

	v->tries++;
	arm(node, v, TIMEOUT);
}

/* Ends a download without a frame; the next query follows one interval later */
static void end(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);

	release(node);
	v->tries = 0;
	set(node, ep, ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_OTA_NORMAL);
	set(node, ep, ZZB_ZCL_OTA_FILE_OFFSET, ZZB_ZCL_OTA_FILE_OFFSET_NONE);
	set(node, ep, ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION,
	    ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION_NONE);
	set(node, ep, ZZB_ZCL_OTA_IMAGE_TYPE_ID, ZZB_ZCL_OTA_IMAGE_TYPE_ID_NONE);
	arm(node, v, INTERVAL);
}

/* Ends a download with Upgrade End Request INVALID_IMAGE or ABORT */
static void fail(struct zzb_node *node, const struct zzb_endpoint *ep, uint8_t status)
{
	end_req(node, ep, status);
	end(node, ep);
}

/* ZCL 11.13.5.4: the download of the file version of size octets starts at offset 0 */
static void start_download(struct zzb_node *node, const struct zzb_endpoint *ep, uint32_t version,
			   uint32_t size)
{
	struct zzb_zcl_ota *v = values_of(ep);

	/* The firmware has one upload slot */
	if (dl.owner != NULL) {
		return;
	}

	dl = (struct download){
		.owner = node,
		.size = size,
		.hdr_len = HEADER_LEN,
	};
	set(node, ep, ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_OTA_DOWNLOADING);
	set(node, ep, ZZB_ZCL_OTA_IMAGE_TYPE_ID, v->image_type);
	set(node, ep, ZZB_ZCL_OTA_DOWNLOADED_FILE_VERSION, version);
	set(node, ep, ZZB_ZCL_OTA_FILE_OFFSET, 0);
	if (flash_img_init(&dl.flash) != 0) {
		fail(node, ep, ZZB_ZCL_ABORT);
		return;
	}

	await_rsp(node, ep);
}

/*
 * ZCL 11.4.2: the identifier, a header version 0x01xx, the image of the Query Next Image Response
 * and its image size. The sub-elements follow the header length.
 */
static uint8_t file_header(const struct zzb_node *node, const struct zzb_zcl_ota *v)
{
	const uint8_t *h = dl.hdr;

	if (sys_get_le32(h) != FILE_ID || sys_get_le16(h + 4) >> 8 != 0x01 ||
	    sys_get_le16(h + 10) != node->device->manufacturer_code ||
	    sys_get_le16(h + 12) != v->image_type_id ||
	    sys_get_le32(h + 14) != v->downloaded_file_version || sys_get_le32(h + 52) != dl.size) {
		return ZZB_ZCL_INVALID_IMAGE;
	}

	dl.left = MAX(sys_get_le16(h + 6), HEADER_LEN) - HEADER_LEN;
	return ZZB_ZCL_SUCCESS;
}

/*
 * ZCL 11.4.3: a sub-element whose data start at offset ends within the file, and the one Upgrade
 * Image has data and fits the upload slot
 */
static uint8_t element(uint32_t offset)
{
	uint16_t tag = sys_get_le16(dl.hdr);
	uint32_t len = sys_get_le32(dl.hdr + 2);

	if (len > dl.size - offset) {
		return ZZB_ZCL_INVALID_IMAGE;
	}

	if (tag == UPGRADE_IMAGE && (dl.image || len == 0 || len > dl.flash.flash_area->fa_size)) {
		return ZZB_ZCL_INVALID_IMAGE;
	}

	dl.image = dl.image || tag == UPGRADE_IMAGE;
	dl.writing = tag == UPGRADE_IMAGE;
	dl.left = len;
	return ZZB_ZCL_SUCCESS;
}

/*
 * Adds n octets at p to the header in hdr and checks it when it is complete; end is the offset in
 * the file after them
 */
static uint8_t collect(const struct zzb_node *node, const struct zzb_zcl_ota *v, uint32_t end,
		       const uint8_t *p, size_t n)
{
	uint8_t status;

	memcpy(dl.hdr + dl.hdr_got, p, n);
	dl.hdr_got += n;
	if (dl.hdr_got < dl.hdr_len) {
		return ZZB_ZCL_SUCCESS;
	}

	status = dl.hdr_len == HEADER_LEN ? file_header(node, v) : element(end);
	dl.hdr_len = ELEMENT_LEN;
	dl.hdr_got = 0;
	return status;
}

/* Passes n octets at p of the data after hdr, which the Upgrade Image writes to the upload slot */
static uint8_t pass(const uint8_t *p, size_t n)
{
	dl.left -= n;
	if (dl.writing && flash_img_buffered_write(&dl.flash, p, n, dl.left == 0) != 0) {
		return ZZB_ZCL_ABORT;
	}

	return ZZB_ZCL_SUCCESS;
}

/*
 * Passes len octets of the file at offset to the parser. Returns INVALID_IMAGE for a file that
 * cannot be valid and ABORT after a flash error.
 */
static uint8_t parse(const struct zzb_node *node, const struct zzb_zcl_ota *v, uint32_t offset,
		     const uint8_t *p, size_t len)
{
	while (len > 0) {
		size_t n = MIN(len, dl.left > 0 ? dl.left : dl.hdr_len - dl.hdr_got);
		uint8_t status = dl.left > 0 ? pass(p, n) : collect(node, v, offset + n, p, n);

		if (status != ZZB_ZCL_SUCCESS) {
			return status;
		}

		offset += n;
		p += n;
		len -= n;
	}

	return ZZB_ZCL_SUCCESS;
}

/*
 * After the last block the file ends with a complete sub-element and has an Upgrade Image, which
 * starts with an MCUboot image header
 */
static bool valid(void)
{
	struct mcuboot_img_header header;

	return dl.left == 0 && dl.hdr_got == 0 && dl.image &&
	       boot_read_bank_header(flash_img_get_upload_slot(), &header, sizeof(header)) == 0;
}

/* After the last block the download is complete, and the client sends Upgrade End Request */
static void complete(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	if (!valid()) {
		fail(node, ep, ZZB_ZCL_INVALID_IMAGE);
		return;
	}

	set(node, ep, ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_OTA_DOWNLOAD_COMPLETE);
	await_rsp(node, ep);
}

/* ZCL 11.16: the client waits for the upgrade time */
static void wait_upgrade(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	struct zzb_zcl_ota *v = values_of(ep);

	v->tries = 0;
	set(node, ep, ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_OTA_WAITING);
	arm(node, v, WAIT);
}

/*
 * At the upgrade time MCUboot is asked to test the new image at the next boot. The node no longer
 * owns the download, and the client sends nothing more.
 */
static void upgrade(struct zzb_node *node, const struct zzb_endpoint *ep)
{
	if (boot_request_upgrade(BOOT_UPGRADE_TEST) != 0) {
		fail(node, ep, ZZB_ZCL_ABORT);
		return;
	}

	release(node);
	if (node->ota_cb != NULL) {
		node->ota_cb(node);
	}
}

/* Armed only on a node with an OTA client */
static void tick(struct k_work *work)
{
	struct zzb_node *node =
		CONTAINER_OF(k_work_delayable_from_work(work), struct zzb_node, zcl.ota_timer);
	const struct zzb_endpoint *ep = first(node);
	struct zzb_zcl_ota *v = values_of(ep);

	switch (v->image_upgrade_status) {
	case ZZB_ZCL_OTA_NORMAL:
		query(node, ep);
		return;
	case ZZB_ZCL_OTA_COUNT_DOWN:
		upgrade(node, ep);
		return;
	}

	/* A request without a response, the end of WAIT_FOR_DATA or the hourly request */
	if (v->tries <= REPEATS) {
		await_rsp(node, ep);
		return;
	}

	if (v->image_upgrade_status == ZZB_ZCL_OTA_DOWNLOADING) {
		end(node, ep);
		return;
	}

	wait_upgrade(node, ep);
}

/* value is own or the wildcard of a manufacturer code or image type (ZCL 11.4.2.5, 11.4.2.6) */
static bool matches(uint16_t value, uint16_t own)
{
	return value == own || value == 0xffff;
}

/* ZCL 11.13.3.4, 11.13.3.5.1: whether a broadcast Image Notify asks the client for a query */
static bool notified(const struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
		     const struct zzb_zcl_ota *v)
{
	/* The payload length of each payload type */
	static const uint8_t lens[] = {2, 4, 6, 10};
	const uint8_t *p = cmd->payload;
	uint32_t version;

	if (cmd->len < 2 || p[0] >= ARRAY_SIZE(lens) || cmd->len < lens[p[0]] ||
	    !IN_RANGE(p[1], 1, 100)) {
		return false;
	}

	if ((p[0] >= 1 && !matches(sys_get_le16(p + 2), node->device->manufacturer_code)) ||
	    (p[0] >= 2 && !matches(sys_get_le16(p + 4), v->image_type))) {
		return false;
	}

	version = p[0] == 3 ? sys_get_le32(p + 6) : 0xffffffff;
	if (version != 0xffffffff &&
	    (version == v->current_file_version || version == v->downloaded_file_version)) {
		return false;
	}

	return sys_rand32_get() % 100 + 1 <= p[1];
}

/* ZCL 11.13.3.4: a unicast starts a query at once, a broadcast after its checks */
static int image_notify(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ota *v)
{
	if (v->image_upgrade_status != ZZB_ZCL_OTA_NORMAL ||
	    (!cmd->unicast && !notified(node, cmd, v))) {
		return ZZB_ZCL_SUCCESS;
	}

	v->server_ep = cmd->src_ep;
	query(node, zzb_node_endpoint(node, cmd->ep));
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 11.13.5 */
static int query_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ota *v)
{
	const uint8_t *p = cmd->payload;
	uint32_t version;

	if (cmd->tsn != v->tsn) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	/* NO_IMAGE_AVAILABLE and NOT_AUTHORIZED end the query */
	if (p[0] != ZZB_ZCL_SUCCESS) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 13 || sys_get_le16(p + 1) != node->device->manufacturer_code ||
	    sys_get_le16(p + 3) != v->image_type || sys_get_le32(p + 9) < HEADER_LEN) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	version = sys_get_le32(p + 5);
	if (version != v->current_file_version && version != v->downloaded_file_version) {
		start_download(node, zzb_node_endpoint(node, cmd->ep), version,
			       sys_get_le32(p + 9));
	}

	return ZZB_ZCL_SUCCESS;
}

/*
 * The time from CurrentTime at p to the time at p + 4 in milliseconds, negative when it is earlier;
 * with CurrentTime 0 the second time is an offset (ZCL 11.13.8.2.8, 11.13.9.6)
 */
static int64_t wait_ms(const uint8_t *p)
{
	return ((int64_t)sys_get_le32(p + 4) - sys_get_le32(p)) * MSEC_PER_SEC;
}

/* ZCL 11.13.8.5.1: a block with the fields of the request; the next request follows at once */
static int block(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ota *v)
{
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, cmd->ep);
	const uint8_t *p = cmd->payload;
	uint8_t status;

	if (cmd->len < 14 || sys_get_le16(p + 1) != node->device->manufacturer_code ||
	    sys_get_le16(p + 3) != v->image_type_id ||
	    sys_get_le32(p + 5) != v->downloaded_file_version ||
	    sys_get_le32(p + 9) != v->file_offset ||
	    !IN_RANGE(p[13], 1, CONFIG_ZZB_ZCL_OTA_BLOCK_SIZE) || cmd->len != 14 + p[13] ||
	    p[13] > dl.size - v->file_offset) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	status = parse(node, v, v->file_offset, p + 14, p[13]);
	if (status != ZZB_ZCL_SUCCESS) {
		fail(node, ep, status);
		return ZZB_ZCL_SUCCESS;
	}

	set(node, ep, ZZB_ZCL_OTA_FILE_OFFSET, v->file_offset + p[13]);
	v->tries = 0;
	if (v->file_offset < dl.size) {
		await_rsp(node, ep);
		return ZZB_ZCL_SUCCESS;
	}

	complete(node, ep);
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 11.13.8.4: WAIT_FOR_DATA sends the same request after RequestTime */
static int wait_for_data(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			 struct zzb_zcl_ota *v)
{
	if (cmd->len < 9 || wait_ms(cmd->payload + 1) < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	v->tries = 0;
	arm(node, v, K_MSEC(wait_ms(cmd->payload + 1)));
	return ZZB_ZCL_SUCCESS;
}

/* ZCL 11.13.8; other statuses are ignored */
static int block_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, struct zzb_zcl_ota *v)
{
	if (v->image_upgrade_status != ZZB_ZCL_OTA_DOWNLOADING || cmd->tsn != v->tsn) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 1) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	switch (cmd->payload[0]) {
	case ZZB_ZCL_SUCCESS:
		return block(node, cmd, v);
	case ZZB_ZCL_WAIT_FOR_DATA:
		return wait_for_data(node, cmd, v);
	case ZZB_ZCL_ABORT:
		end(node, zzb_node_endpoint(node, cmd->ep));
		return ZZB_ZCL_SUCCESS;
	default:
		return ZZB_ZCL_SUCCESS;
	}
}

/*
 * ZCL 11.13.9.8, 11.15.4: a response for the download sets the upgrade time; after the upgrade
 * request the node no longer owns the download
 */
static int upgrade_end_rsp(struct zzb_node *node, const struct zzb_zcl_cmd *cmd,
			   struct zzb_zcl_ota *v)
{
	const struct zzb_endpoint *ep = zzb_node_endpoint(node, cmd->ep);
	const uint8_t *p = cmd->payload;
	uint32_t version;

	if (v->image_upgrade_status < ZZB_ZCL_OTA_DOWNLOAD_COMPLETE || dl.owner != node) {
		return ZZB_ZCL_SUCCESS;
	}

	if (cmd->len < 16) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	version = sys_get_le32(p + 4);
	if (!matches(sys_get_le16(p), node->device->manufacturer_code) ||
	    !matches(sys_get_le16(p + 2), v->image_type_id) ||
	    (version != v->downloaded_file_version && version != 0xffffffff)) {
		return ZZB_ZCL_SUCCESS;
	}

	if (sys_get_le32(p + 12) == 0xffffffff) {
		wait_upgrade(node, ep);
		return ZZB_ZCL_SUCCESS;
	}

	if (wait_ms(p + 8) < 0) {
		return ZZB_ZCL_MALFORMED_COMMAND;
	}

	v->tries = 0;
	set(node, ep, ZZB_ZCL_OTA_IMAGE_UPGRADE_STATUS, ZZB_ZCL_OTA_COUNT_DOWN);
	arm(node, v, K_MSEC(MAX(wait_ms(p + 8), UPGRADE_MIN_MS)));
	return ZZB_ZCL_SUCCESS;
}

/* Only the client on the first endpoint processes commands, and only those of the trust center */
static int cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	if (first(node)->id != cmd->ep) {
		return ZZB_ZCL_UNSUP_COMMAND;
	}

	if (cmd->src_addr != SERVER) {
		return ZZB_ZCL_SUCCESS;
	}

	switch (cmd->id) {
	case ZZB_ZCL_OTA_IMAGE_NOTIFY:
		return image_notify(node, cmd, values);
	case ZZB_ZCL_OTA_QUERY_NEXT_IMAGE_RSP:
		return query_rsp(node, cmd, values);
	case ZZB_ZCL_OTA_IMAGE_BLOCK_RSP:
		return block_rsp(node, cmd, values);
	case ZZB_ZCL_OTA_UPGRADE_END_RSP:
		return upgrade_end_rsp(node, cmd, values);
	default:
		return ZZB_ZCL_UNSUP_COMMAND;
	}
}

/* Nothing is sent at a join; the first query follows one interval after the start */
static void start(struct zzb_node *node, uint8_t ep, void *values)
{
	if (first(node)->id == ep) {
		arm(node, values, INTERVAL);
	}
}

/*
 * The reset of the node ends a download without a frame and restarts the query interval, to
 * endpoint 1 of the trust center
 */
static void reset(struct zzb_node *node, uint8_t ep, void *values)
{
	const struct zzb_endpoint *e = first(node);
	struct zzb_zcl_ota *v = values;

	if (e->id == ep) {
		v->server_ep = SERVER_EP;
		end(node, e);
	}
}

const struct zzb_zcl_cluster_def zzb_zcl_ota_client_def = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
	.init = init,
	.start = start,
	.reset = reset,
	.cmd = cmd,
	.refresh = refresh,
};

void zzb_zcl_ota_init(struct zzb_node *node)
{
	k_work_init_delayable(&node->zcl.ota_timer, tick);
	zzb_nwk_poll_hold(node, ZZB_NWK_HOLD_OTA, false);
	release(node);
}
