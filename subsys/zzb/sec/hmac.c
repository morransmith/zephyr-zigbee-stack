/* SPDX-License-Identifier: Apache-2.0 */

#include "sec/hmac.h"

int zzb_sec_hmac(const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t input, uint8_t mac[ZZB_SEC_KEY_LEN])
{
	uint8_t inner[ZZB_SEC_KEY_LEN + 1];
	uint8_t outer[2 * ZZB_SEC_KEY_LEN];
	int err;

	for (size_t i = 0; i < ZZB_SEC_KEY_LEN; i++) {
		inner[i] = key[i] ^ 0x36;
		outer[i] = key[i] ^ 0x5c;
	}
	inner[ZZB_SEC_KEY_LEN] = input;

	err = zzb_sec_mmo(inner, sizeof(inner), outer + ZZB_SEC_KEY_LEN);
	if (err) {
		return err;
	}

	return zzb_sec_mmo(outer, sizeof(outer), mac);
}
