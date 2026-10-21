/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

/* tests/common/mcuboot_image.bin and the OTA file that scripts/zigbee_ota.py builds from it */
static const uint8_t image[] = {
#include <mcuboot_image.bin.inc>
};

static const uint8_t ota_file[] = {
#include <mcuboot_image.zigbee.inc>
};

/* tests/common/ota_index.json after zigbee_ota.py --index has written the entry of ota_file */
static const char ota_index[] = {
#include <ota_index.json.inc>
};

/* ZCL 11.4.2: the header without the optional fields */
ZTEST(zzb_zcl_ota_file, test_header)
{
	static const char header_string[32] = "zzb";

	zassert_equal(sys_get_le32(&ota_file[0]), 0x0beef11e);
	zassert_equal(sys_get_le16(&ota_file[4]), 0x0100);
	zassert_equal(sys_get_le16(&ota_file[6]), 56);
	zassert_equal(sys_get_le16(&ota_file[8]), 0);
	zassert_equal(sys_get_le16(&ota_file[10]), 0x1234);
	zassert_equal(sys_get_le16(&ota_file[12]), 0x0042);
	zassert_equal(sys_get_le32(&ota_file[14]), 0x01020400);
	zassert_equal(sys_get_le16(&ota_file[18]), 0x0002);
	zassert_mem_equal(&ota_file[20], header_string, sizeof(header_string));
	zassert_equal(sys_get_le32(&ota_file[52]), sizeof(ota_file));
}

/* ZCL 11.4.3: one Upgrade Image sub-element with the MCUboot image */
ZTEST(zzb_zcl_ota_file, test_upgrade_image)
{
	zassert_equal(sizeof(ota_file), 56 + 6 + sizeof(image));
	zassert_equal(sys_get_le16(&ota_file[56]), 0x0000);
	zassert_equal(sys_get_le32(&ota_file[58]), sizeof(image));
	zassert_mem_equal(&ota_file[62], image, sizeof(image));
}

/* The entry of another version stays, and the entry of the same version is replaced */
ZTEST(zzb_zcl_ota_file, test_index)
{
	static const char expected[] =
		"{\n"
		"  \"firmwares\": [\n"
		"    {\n"
		"      \"path\": \"older.zigbee\",\n"
		"      \"file_version\": 16909056,\n"
		"      \"file_size\": 212,\n"
		"      \"image_type\": 66,\n"
		"      \"manufacturer_id\": 4660,\n"
		"      \"checksum\": \"sha3-256:"
		"0000000000000000000000000000000000000000000000000000000000000000\"\n"
		"    },\n"
		"    {\n"
		"      \"path\": \"mcuboot_image.zigbee\",\n"
		"      \"file_version\": 16909312,\n"
		"      \"file_size\": 212,\n"
		"      \"image_type\": 66,\n"
		"      \"manufacturer_id\": 4660,\n"
		"      \"checksum\": \"sha3-256:"
		"6efcee86aedc9d78272e82146155c77f875eb3182ce582c53f1797fec53a770f\"\n"
		"    }\n"
		"  ]\n"
		"}\n";

	zassert_equal(sizeof(ota_index), sizeof(expected) - 1);
	zassert_mem_equal(ota_index, expected, sizeof(expected) - 1);
}

ZTEST_SUITE(zzb_zcl_ota_file, NULL, NULL, NULL, NULL, NULL);
