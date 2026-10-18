/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_COMMON_NODE_H_
#define ZZB_SUBSYS_COMMON_NODE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zzb/zzb.h>

struct zzb_node {
	bool in_use;
	uint64_t eui64;
};

#endif /* ZZB_SUBSYS_COMMON_NODE_H_ */
