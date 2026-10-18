/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "common/queue.h"

struct call {
	struct k_work work;
	k_tid_t thread;
	bool done;
};

static void record_handler(struct k_work *work)
{
	struct call *call = CONTAINER_OF(work, struct call, work);

	call->thread = k_current_get();
	call->done = true;
}

ZTEST(zzb_queue, test_call_sync_runs_on_stack_thread)
{
	struct call call = {0};

	zzb_call_sync(&call.work, record_handler);

	zassert_true(call.done);
	zassert_equal(call.thread, k_work_queue_thread_get(&zzb_workq));
}

struct nested {
	struct k_work work;
	struct call inner;
	bool inner_done_on_return;
};

static void nested_handler(struct k_work *work)
{
	struct nested *nested = CONTAINER_OF(work, struct nested, work);

	zzb_call_sync(&nested->inner.work, record_handler);
	nested->inner_done_on_return = nested->inner.done;
}

ZTEST(zzb_queue, test_call_sync_on_stack_thread_runs_inline)
{
	struct nested nested = {0};

	zzb_call_sync(&nested.work, nested_handler);

	zassert_true(nested.inner_done_on_return);
}

ZTEST_SUITE(zzb_queue, NULL, NULL, NULL, NULL, NULL);
