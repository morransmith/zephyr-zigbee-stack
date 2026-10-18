/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "fakes.h"

ZTEST(zzb_mac, test_init_binds_radio_and_programs_filters)
{
	zassert_equal_ptr(node->port.dev, radio_dev);
	zassert_equal(radio.ext_addr, NODE_EUI64);
	zassert_equal(radio.pan_id, 0xffff);
	zassert_equal(radio.short_addr, 0xffff);
	zassert_equal(radio.channel, 11);
	zassert_true(radio.started);
}

ZTEST(zzb_mac, test_init_without_radio_is_invalid)
{
	const struct zzb_node_config cfg = {
		.eui64 = NODE_EUI64,
	};
	struct zzb_node *other;

	zassert_equal(zzb_node_init(&cfg, &other), -EINVAL);
	zassert_is_null(other);
}

ZTEST_SUITE(zzb_mac, NULL, mac_setup, mac_before, NULL, NULL);
