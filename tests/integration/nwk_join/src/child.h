/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_TESTS_INTEGRATION_NWK_JOIN_CHILD_H_
#define ZZB_TESTS_INTEGRATION_NWK_JOIN_CHILD_H_

#include <stdint.h>

#define PARENT_ADDR  0x0000
#define PARENT_EUI64 0x00124b0000000001ULL
#define CHILD_ADDR   0x1234

/* The zzb node on fake radio 1 */
extern struct zzb_node *child;

/* What the APS callbacks of the child received; the test does what ZDO and BDB will do */
struct child_log {
	int transport_keys;
	int confirm_keys;
	uint8_t confirm_status;
	int data_confs;
	uint8_t data_status;
	int data_inds;
};

extern struct child_log child_log;

/* Initializes the child on the first call */
void child_init(void);

/* Starts the parent, resets the child and joins it: discovery, association, network key */
void child_join(void);

#endif /* ZZB_TESTS_INTEGRATION_NWK_JOIN_CHILD_H_ */
