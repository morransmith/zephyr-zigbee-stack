/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_KEY_CACHE_H_
#define ZZB_SUBSYS_SEC_KEY_CACHE_H_

#include <stdint.h>
#include <psa/crypto.h>

#include "sec/mmo.h"

/*
 * Sets id to the PSA key with these bytes, imported for CCM with a MIC of 4 bytes or more. The
 * key is imported into the least recently used entry if no entry holds it. id is valid until the
 * next call. Returns -EIO if the import fails.
 */
int zzb_sec_key_get(const uint8_t key[ZZB_SEC_KEY_LEN], psa_key_id_t *id);

#endif /* ZZB_SUBSYS_SEC_KEY_CACHE_H_ */
