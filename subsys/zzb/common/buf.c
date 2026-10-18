/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/net_buf.h>

#include "common/buf.h"

NET_BUF_POOL_DEFINE(zzb_buf_pool, CONFIG_ZZB_BUF_COUNT, ZZB_BUF_SIZE, sizeof(struct zzb_frame_meta),
		    NULL);

struct net_buf *zzb_buf_alloc(k_timeout_t timeout)
{
	return net_buf_alloc(&zzb_buf_pool, timeout);
}
