/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include "sec/install_code.h"

int zzb_sec_install_code_key(const uint8_t *code, size_t len, uint8_t key[ZZB_SEC_KEY_LEN])
{
	/* CRC-16/X-25, stored little-endian */
	if (len < 3 ||
	    (crc16_ccitt(0xffff, code, len - 2) ^ 0xffff) != sys_get_le16(code + len - 2)) {
		return -EINVAL;
	}

	return zzb_sec_mmo(code, len, key);
}
