/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_COMMON_FAKE_RADIO_H_
#define ZZB_TESTS_COMMON_FAKE_RADIO_H_

#include <zephyr/device.h>

#define FAKE_RADIO_COUNT 2

/* Radio devices for the custom-L2 backend, connected by an in-process medium */
extern const struct device *const fake_radios[FAKE_RADIO_COUNT];

#endif /* ZZB_TESTS_COMMON_FAKE_RADIO_H_ */
