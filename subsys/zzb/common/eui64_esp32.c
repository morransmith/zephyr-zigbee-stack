/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <esp_mac.h>
#include <zephyr/sys/byteorder.h>

#include "common/eui64.h"

int zzb_eui64_esp32_get(uint64_t *eui64)
{
	uint8_t mac[8];

	if (esp_efuse_mac_get_default(mac) != ESP_OK) {
		return -ENODEV;
	}

	*eui64 = sys_get_be64(mac);
	return 0;
}
