/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_NODE_H_
#define ZZB_SUBSYS_COMMON_NODE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zzb.h>

#include "mac/mac.h"
#include "radio/port.h"

struct zzb_node {
	bool in_use;
	uint64_t eui64;
	struct zzb_radio_port port;
	struct zzb_mac mac;
};

#endif /* ZZB_SUBSYS_COMMON_NODE_H_ */
