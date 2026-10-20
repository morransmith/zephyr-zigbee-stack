/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>
#include <zzb/zcl/ota.h>

#include "fakes.h"
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

/* Passes a frame of OTA Upgrade from the requester to endpoint ep of the fourth node */
static void receive_ota(uint8_t ep, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ep,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = ZZB_ZCL_OTA,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(ota_node, buf, &ind);
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

static void before(void *fixture)
{
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
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

ZTEST_SUITE(zzb_zcl_ota, NULL, setup, before, after, NULL);
