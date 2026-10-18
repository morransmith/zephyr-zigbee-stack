/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_MMO_H_
#define ZZB_SUBSYS_SEC_MMO_H_

#include <stddef.h>
#include <stdint.h>

/* AES-128 key and block length, also the MMO hash length */
#define ZZB_SEC_KEY_LEN 16

/* Matyas-Meyer-Oseas hash (R22 B.6). Returns -EINVAL for messages of 8192 bytes or more. */
int zzb_sec_mmo(const uint8_t *msg, size_t len, uint8_t hash[ZZB_SEC_KEY_LEN]);

#endif /* ZZB_SUBSYS_SEC_MMO_H_ */
