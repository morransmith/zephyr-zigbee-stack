/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_RADIO_PORT_H_
#define ZZB_SUBSYS_RADIO_PORT_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net_buf.h>

struct zzb_radio_port {
	const struct device *dev;
	struct k_fifo rx_fifo;
	struct k_work rx_work;
};

/* Binds port to the radio device dev. */
int zzb_radio_init(struct zzb_radio_port *port, const struct device *dev);

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
