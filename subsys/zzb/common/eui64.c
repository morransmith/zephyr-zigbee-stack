/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/util.h>

#include "common/eui64.h"

int zzb_eui64_get(uint64_t *eui64)
{
	uint8_t id[8];
	ssize_t len;

	if (IS_ENABLED(CONFIG_DT_HAS_ESPRESSIF_ESP32_IEEE802154_ENABLED) &&
	    zzb_eui64_esp32_get(eui64) == 0) {
		return 0;
	}

	len = hwinfo_get_device_id(id, sizeof(id));
	if (len <= 0) {
		return -ENODEV;
	}

	*eui64 = 0;
	for (ssize_t i = 0; i < len; i++) {
		*eui64 = (*eui64 << 8) | id[i];
	}

	if (len == 6) {
		/* MAC-48: insert FF:FE after the OUI */
		*eui64 = (*eui64 & 0xffffff000000ULL) << 16 | 0xfffe000000ULL |
			 (*eui64 & 0xffffffULL);
	}

	return 0;
}
