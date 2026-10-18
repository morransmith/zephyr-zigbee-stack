/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_RADIO_PORT_H_
#define ZZB_SUBSYS_RADIO_PORT_H_

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net_buf.h>

#include "mac/mac.h"

struct zzb_radio_port {
	const struct device *dev;
	bool hw_csma;
	/* Passed to every tx() with the frame as the fragment */
	struct net_pkt tx_pkt;
	struct net_buf *tx_buf;
	struct k_work tx_work;
	struct k_fifo rx_fifo;
	struct k_work rx_work;
};

/*
 * Binds port to the radio device dev. Returns -ENOTSUP if the driver does not wait for the ACK
 * in tx(), or -ENODEV if the custom-L2 backend finds no interface for dev.
 */
int zzb_radio_init(struct zzb_radio_port *port, const struct device *dev);

/*
 * Queues the frame in buf for transmission. Returns -EBUSY, leaving buf with the caller, until
 * the confirm of the previous frame.
 */
int zzb_radio_tx(struct zzb_radio_port *port, struct net_buf *buf);

/* Defined by the MAC. Called on the stack thread with the frame given to zzb_radio_tx(). */
void zzb_radio_tx_done(struct zzb_radio_port *port, struct net_buf *buf, enum zzb_mac_status status,
		       bool frame_pending);

/* Defined by the MAC. Called on the stack thread with each received frame. */
void zzb_radio_rx(struct zzb_radio_port *port, struct net_buf *buf);

/*
 * RX entry of the backends, callable from ISR context. Copies the frame and releases pkt, or
 * returns -ENOBUFS and leaves pkt with the caller.
 */
int zzb_radio_recv(struct zzb_radio_port *port, struct net_pkt *pkt);

/* Defined by the backend: passes the frames received by port->dev to zzb_radio_recv(). */
int zzb_radio_attach(struct zzb_radio_port *port);

#endif /* ZZB_SUBSYS_RADIO_PORT_H_ */
