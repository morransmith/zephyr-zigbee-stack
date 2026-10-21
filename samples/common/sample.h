/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SAMPLES_COMMON_SAMPLE_H_
#define ZZB_SAMPLES_COMMON_SAMPLE_H_

#include <stdbool.h>
#include <zephyr/sys/util.h>
#include <zzb/zzb.h>

/** Power source of the device definition: a battery for a sleepy end device, mains otherwise. */
#define SAMPLE_POWER_SOURCE                                                                        \
	(IS_ENABLED(CONFIG_SAMPLE_SLEEPY) ? ZZB_POWER_DISPOSABLE : ZZB_POWER_MAINS)

/** PowerSource of the Basic server (ZCL Table 3-8): battery or single phase mains. */
#define SAMPLE_BASIC_POWER_SOURCE (IS_ENABLED(CONFIG_SAMPLE_SLEEPY) ? 0x03 : 0x01)

/**
 * @brief Set up the LED and the button and initialize the node.
 *
 * The application calls zzb_bdb_start() next.
 *
 * @return the error of zzb_node_init().
 */
int sample_init(const struct zzb_node_config *cfg, struct zzb_node **node);

/**
 * BDB callback of the samples. It logs the event, keeps whether the node is on a network and starts
 * network steering when the node is off a network after zzb_bdb_start() and after it has left.
 */
void sample_bdb_event(struct zzb_node *node, enum zzb_bdb_event event, enum zzb_bdb_status status);

/** Whether the node is on a network (bdbNodeIsOnANetwork). */
bool sample_on_network(void);

/** Turn led0 on or off, if the board has it. */
void sample_led(bool on);

/** Blink led0 while on is true, then show the state of sample_led() again. */
void sample_identify(bool on);

/** Defined by the sample: the action of a short press of the button on a network. */
void sample_button(struct zzb_node *node);

/** Defined by the sample: the finding & binding of a hold of the button for 3 s. */
void sample_finding_binding(struct zzb_node *node);

#endif /* ZZB_SAMPLES_COMMON_SAMPLE_H_ */
