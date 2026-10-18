/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "sec/key_cache.h"

struct entry {
	uint8_t key[ZZB_SEC_KEY_LEN];
	psa_key_id_t id;
	/* 0 for an empty entry */
	uint32_t last_use;
};

static struct entry cache[CONFIG_ZZB_KEY_CACHE_SIZE];
static uint32_t use_count;

static int import(struct entry *e, const uint8_t key[ZZB_SEC_KEY_LEN])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_destroy_key(e->id);
	e->id = PSA_KEY_ID_NULL;
	e->last_use = 0;

	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_AT_LEAST_THIS_LENGTH_TAG(PSA_ALG_CCM, 4));
	if (psa_import_key(&attr, key, ZZB_SEC_KEY_LEN, &e->id) != PSA_SUCCESS) {
		return -EIO;
	}

	memcpy(e->key, key, ZZB_SEC_KEY_LEN);
	return 0;
}

int zzb_sec_key_get(const uint8_t key[ZZB_SEC_KEY_LEN], psa_key_id_t *id)
{
	struct entry *lru = &cache[0];
	struct entry *e = NULL;

	ARRAY_FOR_EACH_PTR(cache, c) {
		if (c->last_use != 0 && memcmp(c->key, key, ZZB_SEC_KEY_LEN) == 0) {
			e = c;
			break;
		}

		if (c->last_use < lru->last_use) {
			lru = c;
		}
	}

	if (e == NULL) {
		if (import(lru, key)) {
			return -EIO;
		}
		e = lru;
	}

	e->last_use = ++use_count;
	*id = e->id;
	return 0;
}
