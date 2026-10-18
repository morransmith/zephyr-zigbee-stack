/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/fff.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DEFINE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);

void reset_fakes(void)
{
	RESET_FAKE(zzb_mlme_set_pan_id);
	RESET_FAKE(zzb_mlme_set_short_addr);
	FFF_RESET_HISTORY();
}
