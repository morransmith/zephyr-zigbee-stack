/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_COMMON_FAKE_RADIO_H_
#define ZZB_TESTS_COMMON_FAKE_RADIO_H_

#include <stdbool.h>
#include <zephyr/device.h>

#define FAKE_RADIO_COUNT 2

/* Radio devices for the custom-L2 backend, connected by an in-process medium */
extern const struct device *const fake_radios[FAKE_RADIO_COUNT];

/* Restores all links and the filters: PAN ID 0xffff, short address 0xfffe, IEEE address 0 */
void fake_radio_reset(void);

/* Sets whether radio "to" receives the frames of radio "from" */
void fake_radio_set_link(int from, int to, bool up);

#endif /* ZZB_TESTS_COMMON_FAKE_RADIO_H_ */
