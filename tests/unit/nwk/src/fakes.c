/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);

struct zzb_node *test_node(int index)
{
	static const uint64_t eui64[] = {NODE_EUI64, SENDER_EUI64};
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
	RESET_FAKE(zzb_mlme_set_pan_id);
	RESET_FAKE(zzb_mlme_set_short_addr);
	FFF_RESET_HISTORY();
}
