/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_UNIT_MAC_FAKES_H_
#define ZZB_TESTS_UNIT_MAC_FAKES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/net_buf.h>

#include "common/buf.h"
#include "mac/mac.h"

#define NODE_EUI64 0x0011223344556677ULL

/* The parent played by the tx() fake */
#define PAN_ID       0x1a62
#define PARENT_SHORT 0x0000

/* What the MAC has programmed into the test radio */
struct radio_state {
	uint64_t ext_addr;
	uint16_t pan_id;
	uint16_t short_addr;
	uint16_t channel;
	bool started;
};

struct sent_frame {
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	/* The radio when the frame was sent */
	struct radio_state radio;
};

/* Behavior of the parent, reset before each test */
struct parent {
	/* Number of frames with the ACK request bit that get no ACK */
	int lost;
	/* tx() on this channel returns -EBUSY, as after a CSMA-CA failure */
	uint16_t busy_channel;
	/* Frame received after the command reply_to, on reply_channel or on any channel if 0 */
	const uint8_t *reply;
	size_t reply_len;
	uint8_t reply_to;
	uint16_t reply_channel;
};

/* Frame passed up by an indication */
struct rx_copy {
	uint8_t data[ZZB_BUF_SIZE];
	size_t len;
	struct zzb_frame_meta meta;
};

extern const struct device *const radio_dev;
extern struct radio_state radio;
extern struct sent_frame sent[8];
extern int sent_count;
extern struct parent parent;
extern struct zzb_node *node;
extern struct rx_copy data_ind;
extern struct rx_copy beacon;

DECLARE_FAKE_VOID_FUNC(zzb_mcps_data_conf, struct zzb_node *, uint8_t, enum zzb_mac_status);
DECLARE_FAKE_VOID_FUNC(zzb_mcps_data_ind, struct zzb_node *, struct net_buf *);
DECLARE_FAKE_VOID_FUNC(zzb_mlme_scan_conf, struct zzb_node *, enum zzb_mac_status);
DECLARE_FAKE_VOID_FUNC(zzb_mlme_beacon_notify_ind, struct zzb_node *, struct net_buf *, uint8_t,
		       uint16_t);

/* Suite hooks shared by the MAC suites: one node, its MAC initialized again before each test */
void *mac_setup(void);
void mac_before(void *fixture);
/* Checks that the MAC is idle and has released every buffer */
void mac_after(void *fixture);

/* Runs the stack thread until it waits, without advancing the time */
void run_stack(void);

/* Passes frame to the radio port as a received frame */
void inject(const uint8_t *frame, size_t len);

/* Returns a buffer with the MSDU for MCPS-DATA.request to the parent, with ACK request */
struct net_buf *msdu_buf(uint8_t handle, const uint8_t *msdu, size_t len);

/* Compares sent frame i with frame, except its sequence number */
void check_sent(int i, const uint8_t *frame, size_t len);

#endif /* ZZB_TESTS_UNIT_MAC_FAKES_H_ */
