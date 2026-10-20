/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_TIME_H_
#define ZZB_INCLUDE_ZZB_ZCL_TIME_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Time cluster (ZCL 3.12). */
#define ZZB_ZCL_TIME 0x000a

/**
 * @brief Get the time of the node.
 *
 * Runs on the stack thread. Times are seconds since 2000-01-01 00:00 UTC (ZCL 2.6.2.24); the local
 * time has the time zone and daylight saving time of the last setting of the clock.
 *
 * @retval 0 on success.
 * @retval -ENODATA the clock of the node has not been set since the start or the reset of the node.
 */
int zzb_zcl_time_get(struct zzb_node *node, uint32_t *utc, uint32_t *local);

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_TIME_H_ */
