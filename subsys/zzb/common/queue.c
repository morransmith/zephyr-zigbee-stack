/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "common/queue.h"

struct k_work_q zzb_workq;

static K_THREAD_STACK_DEFINE(workq_stack, CONFIG_ZZB_WORKQ_STACK_SIZE);

static int workq_init(void)
{
	const struct k_work_queue_config cfg = {
		.name = "zzb",
	};

	k_work_queue_start(&zzb_workq, workq_stack, K_THREAD_STACK_SIZEOF(workq_stack),
			   K_PRIO_COOP(CONFIG_ZZB_WORKQ_PRIORITY), &cfg);
	return 0;
}

SYS_INIT(workq_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

void zzb_call_sync(struct k_work *work, k_work_handler_t handler)
{
	struct k_work_sync sync;

	if (k_current_get() == k_work_queue_thread_get(&zzb_workq)) {
		handler(work);
		return;
	}

	k_work_init(work, handler);
	k_work_submit_to_queue(&zzb_workq, work);
	k_work_flush(work, &sync);
}
