/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

const uint8_t key_b[ZZB_SEC_KEY_LEN] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

struct zzb_node *test_node(int index)
{
	static const uint64_t eui64[] = {NODE_EUI64, TC_EUI64};
	static struct zzb_node *nodes[ARRAY_SIZE(eui64)];

	if (nodes[0] == NULL) {
		ARRAY_FOR_EACH(nodes, i) {
			const struct zzb_node_config cfg = {
				.eui64 = eui64[i],
			};

			zassert_ok(zzb_node_init(&cfg, &nodes[i]));
		}
	}

	return nodes[index];
}

void reset_fakes(void)
{
	FFF_RESET_HISTORY();
}

/* The test thread is cooperative and has a lower priority than the stack thread */
void run_stack(void)
{
	k_yield();
}
