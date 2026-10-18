/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_COMMON_FAKE_RADIO_H_
#define ZZB_TESTS_COMMON_FAKE_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#define FAKE_RADIO_COUNT 2

/*
 * Radio devices for the custom-L2 backend, connected by an in-process medium. A frame reaches the
 * radios on the channel of its sender.
 */
extern const struct device *const fake_radios[FAKE_RADIO_COUNT];

/*
 * Restores all links, the interfaces and the filters: PAN ID 0xffff, short address 0xfffe, IEEE
 * address 0
 */
void fake_radio_reset(void);

/* Sets whether radio "to" receives the frames of radio "from" */
void fake_radio_set_link(int from, int to, bool up);

/* Receives a frame that a radio accepts and returns the frame pending bit of its ACK */
typedef bool (*fake_radio_rx_cb)(const uint8_t *frame, size_t len);

/* Passes the frames that radio accepts to cb instead of its interface; NULL restores it */
void fake_radio_set_rx_cb(int radio, fake_radio_rx_cb cb);

/* Sends frame from radio "from" to the radios on its channel that accept it, without an ACK */
void fake_radio_send(int from, const uint8_t *frame, size_t len);

#endif /* ZZB_TESTS_COMMON_FAKE_RADIO_H_ */
