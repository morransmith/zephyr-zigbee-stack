/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_INSTALL_CODE_H_
#define ZZB_SUBSYS_SEC_INSTALL_CODE_H_

#include <stddef.h>
#include <stdint.h>

#include "sec/mmo.h"

/*
 * Derives the link key from an install code (BDB 10.1). len includes the CRC in the last 2 bytes.
 * Returns -EINVAL when the CRC does not match.
 */
int zzb_sec_install_code_key(const uint8_t *code, size_t len, uint8_t key[ZZB_SEC_KEY_LEN]);

#endif /* ZZB_SUBSYS_SEC_INSTALL_CODE_H_ */
