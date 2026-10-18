/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_NWK_FAKES_H_
#define ZZB_TESTS_UNIT_NWK_FAKES_H_

#include <stdint.h>
#include <zephyr/fff.h>

#include "mac/mac.h"

DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_pan_id, struct zzb_node *, uint16_t);
DECLARE_FAKE_VOID_FUNC(zzb_mlme_set_short_addr, struct zzb_node *, uint16_t);

/* Resets every fake */
void reset_fakes(void);

#endif /* ZZB_TESTS_UNIT_NWK_FAKES_H_ */
