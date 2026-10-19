/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "bdb/bdb.h"
#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_nlme_join_req, struct zzb_node *, uint64_t,
		       enum zzb_nwk_join_method, uint32_t, uint8_t, uint8_t, bool);
DEFINE_FAKE_VALUE_FUNC(uint64_t, zzb_nlme_get_ext_pan_id, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(uint8_t, zzb_nlme_get_capability, const struct zzb_node *);
DEFINE_FAKE_VALUE_FUNC(int, zzb_zdo_device_annce_req, struct zzb_node *);

struct event_log events;

static void record_event(struct zzb_node *node, enum zzb_bdb_event event,
			 enum zzb_bdb_status status)
{
	events.count++;
	events.event = event;
	events.status = status;
}

struct zzb_node *test_node(void)
{
	static struct zzb_node *node;
	const struct zzb_node_config cfg = {
		.eui64 = 0x00124b0001020304ULL,
		.bdb_cb = record_event,
	};

	if (node == NULL) {
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return node;
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_nlme_join_req);
	RESET_FAKE(zzb_nlme_get_ext_pan_id);
	RESET_FAKE(zzb_nlme_get_capability);
	RESET_FAKE(zzb_zdo_device_annce_req);
	FFF_RESET_HISTORY();
	events = (struct event_log){0};

	zzb_nlme_get_capability_fake.return_val = CAPABILITY;
}

static void reboot_handler(struct k_work *work)
{
	zzb_bdb_init(test_node());
}

void reboot(bool on_network)
{
	struct k_work work;

	zzb_nlme_get_ext_pan_id_fake.return_val = on_network ? EXT_PAN_ID : 0;
	zzb_call_sync(&work, reboot_handler);
}

/* The test thread is cooperative and has a lower priority than the stack thread */
void run_stack(void)
{
	k_yield();
}
