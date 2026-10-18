/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_SEC_HMAC_H_
#define ZZB_SUBSYS_SEC_HMAC_H_

#include <stdint.h>

#include "sec/mmo.h"

/* HMAC-MMO of a 1-octet input (R22 B.1.4), the form used by R22 4.5.3 and 4.4.10.7.4 */
int zzb_sec_hmac(const uint8_t key[ZZB_SEC_KEY_LEN], uint8_t input, uint8_t mac[ZZB_SEC_KEY_LEN]);

#endif /* ZZB_SUBSYS_SEC_HMAC_H_ */
