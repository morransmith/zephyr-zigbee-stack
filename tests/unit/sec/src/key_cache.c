/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>

#include "sec/ccm.h"
#include "sec/key_cache.h"

static const uint8_t nonce[ZZB_SEC_NONCE_LEN];
static const uint8_t msg[] = {0x01, 0x02, 0x03};

static void make_key(uint8_t key[ZZB_SEC_KEY_LEN], uint8_t n)
{
	memset(key, n, ZZB_SEC_KEY_LEN);
}

static int encrypt(psa_key_id_t id, uint8_t out[sizeof(msg) + ZZB_SEC_MIC_LEN])
{
	memcpy(out, msg, sizeof(msg));
	return zzb_sec_ccm_encrypt(id, nonce, NULL, 0, out, sizeof(msg), ZZB_SEC_MIC_LEN);
}

ZTEST(zzb_key_cache, test_key_is_imported_once)
{
	uint8_t buf[sizeof(msg) + ZZB_SEC_MIC_LEN];
	uint8_t key[ZZB_SEC_KEY_LEN];
	psa_key_id_t id;
	psa_key_id_t again;

	make_key(key, 0xa0);
	zassert_ok(zzb_sec_key_get(key, &id));
	zassert_ok(zzb_sec_key_get(key, &again));
	zassert_equal(again, id);

	zassert_ok(encrypt(id, buf));
	zassert_ok(zzb_sec_ccm_decrypt(id, nonce, NULL, 0, buf, sizeof(buf), ZZB_SEC_MIC_LEN));
	zassert_mem_equal(buf, msg, sizeof(msg));
}

/* PSA may give a new key the ID of a destroyed one, so the IDs are checked by encrypting */
ZTEST(zzb_key_cache, test_least_recently_used_key_is_evicted)
{
	uint8_t ciphertexts[CONFIG_ZZB_KEY_CACHE_SIZE][sizeof(msg) + ZZB_SEC_MIC_LEN];
	uint8_t buf[sizeof(msg) + ZZB_SEC_MIC_LEN];
	psa_key_id_t ids[CONFIG_ZZB_KEY_CACHE_SIZE];
	uint8_t key[ZZB_SEC_KEY_LEN];
	psa_key_id_t id;

	/* Fills the cache, then uses key 0 again so that key 1 is the least recently used */
	ARRAY_FOR_EACH(ids, i) {
		make_key(key, i);
		zassert_ok(zzb_sec_key_get(key, &ids[i]));
		zassert_ok(encrypt(ids[i], ciphertexts[i]));
	}

	make_key(key, 0);
	zassert_ok(zzb_sec_key_get(key, &id));

	make_key(key, CONFIG_ZZB_KEY_CACHE_SIZE);
	zassert_ok(zzb_sec_key_get(key, &id));

	zassert_true(encrypt(ids[1], buf) != 0 || memcmp(buf, ciphertexts[1], sizeof(buf)) != 0);
	ARRAY_FOR_EACH(ids, i) {
		if (i == 1) {
			continue;
		}

		zassert_ok(encrypt(ids[i], buf));
		zassert_mem_equal(buf, ciphertexts[i], sizeof(buf), "key %zu", i);
	}
}

ZTEST_SUITE(zzb_key_cache, NULL, NULL, NULL, NULL, NULL);
