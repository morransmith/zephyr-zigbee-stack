/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_QUEUE_H_
#define ZZB_SUBSYS_COMMON_QUEUE_H_

#include <zephyr/kernel.h>

extern struct k_work_q zzb_workq;

/* Runs handler on the stack thread and returns when it has finished. */
void zzb_call_sync(struct k_work *work, k_work_handler_t handler);

#endif /* ZZB_SUBSYS_COMMON_QUEUE_H_ */
