/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_EUI64_H_
#define ZZB_SUBSYS_COMMON_EUI64_H_

#include <stdint.h>

/* Reads the EUI-64 from the ESP32 eFuse, then from hwinfo. */
int zzb_eui64_get(uint64_t *eui64);

int zzb_eui64_esp32_get(uint64_t *eui64);

#endif /* ZZB_SUBSYS_COMMON_EUI64_H_ */
