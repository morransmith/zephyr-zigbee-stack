/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>

#include "common/buf.h"

ZTEST(zzb_buf, test_alloc_resets_reused_buffer)
{
	static const struct zzb_frame_meta zero;
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	memset(zzb_frame_meta(buf), 0xff, sizeof(struct zzb_frame_meta));
	net_buf_add(buf, 10);
	net_buf_unref(buf);

	buf = zzb_buf_alloc(K_NO_WAIT);
	zassert_not_null(buf);
	zassert_mem_equal(zzb_frame_meta(buf), &zero, sizeof(zero));
	zassert_equal(net_buf_tailroom(buf), ZZB_BUF_SIZE);
	net_buf_unref(buf);
}

ZTEST_SUITE(zzb_buf, NULL, NULL, NULL, NULL, NULL);
