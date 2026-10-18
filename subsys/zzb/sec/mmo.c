/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>

#include "sec/mmo.h"

/* Longer messages need the second padding method of R22 B.6 */
#define MMO_MAX_LEN 8191

/* Hash_j = E(Hash_j-1, M_j) xor M_j */
static int mmo_block(uint8_t hash[ZZB_SEC_KEY_LEN], const uint8_t block[ZZB_SEC_KEY_LEN])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key;
	psa_status_t status;
	size_t out_len;

	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);

	if (psa_import_key(&attr, hash, ZZB_SEC_KEY_LEN, &key) != PSA_SUCCESS) {
		return -EIO;
	}

	status = psa_cipher_encrypt(key, PSA_ALG_ECB_NO_PADDING, block, ZZB_SEC_KEY_LEN, hash,
				    ZZB_SEC_KEY_LEN, &out_len);
	psa_destroy_key(key);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}

	for (size_t i = 0; i < ZZB_SEC_KEY_LEN; i++) {
		hash[i] ^= block[i];
	}

	return 0;
}

int zzb_sec_mmo(const uint8_t *msg, size_t len, uint8_t hash[ZZB_SEC_KEY_LEN])
{
	size_t tail = len % ZZB_SEC_KEY_LEN;
	uint8_t block[ZZB_SEC_KEY_LEN] = {0};
	int err;

	if (len > MMO_MAX_LEN) {
		return -EINVAL;
	}

	memset(hash, 0, ZZB_SEC_KEY_LEN);

	for (size_t i = 0; i < len - tail; i += ZZB_SEC_KEY_LEN) {
		err = mmo_block(hash, msg + i);
		if (err) {
			return err;
		}
	}

	/* Bit '1', zeros up to 14 mod 16 octets, then the 16-bit length in bits */
	memcpy(block, msg + len - tail, tail);
	block[tail] = 0x80;

	if (tail >= ZZB_SEC_KEY_LEN - 2) {
		err = mmo_block(hash, block);
		if (err) {
			return err;
		}
		memset(block, 0, sizeof(block));
	}

	sys_put_be16(len * 8, block + ZZB_SEC_KEY_LEN - 2);
	return mmo_block(hash, block);
}
