/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include "common/queue.h"
#include "common/timer.h"

void zzb_timer_arm(struct k_work_delayable *dwork, k_timeout_t delay)
{
	/* K_WORK_DELAYED excludes RUNNING, so a handler can re-arm itself */
	if ((k_work_delayable_busy_get(dwork) & K_WORK_DELAYED) &&
	    k_work_delayable_remaining_get(dwork) <= delay.ticks) {
		return;
	}

	k_work_reschedule_for_queue(&zzb_workq, dwork, delay);
}
