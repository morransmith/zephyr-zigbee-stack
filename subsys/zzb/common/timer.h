/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_TIMER_H_
#define ZZB_SUBSYS_COMMON_TIMER_H_

#include <zephyr/kernel.h>

/* Schedules dwork on the stack queue unless it already expires earlier. */
void zzb_timer_arm(struct k_work_delayable *dwork, k_timeout_t delay);

#endif /* ZZB_SUBSYS_COMMON_TIMER_H_ */
