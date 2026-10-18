/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_MAC_FAKES_H_
#define ZZB_TESTS_UNIT_MAC_FAKES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#define NODE_EUI64 0x0011223344556677ULL

/* What the MAC has programmed into the test radio */
struct radio_state {
	uint64_t ext_addr;
	uint16_t pan_id;
	uint16_t short_addr;
	uint16_t channel;
	bool started;
};

extern const struct device *const radio_dev;
extern struct radio_state radio;
extern struct zzb_node *node;

/* Suite hooks shared by the MAC suites: one node, its MAC initialized again before each test */
void *mac_setup(void);
void mac_before(void *fixture);

#endif /* ZZB_TESTS_UNIT_MAC_FAKES_H_ */
