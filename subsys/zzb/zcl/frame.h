/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_SUBSYS_ZCL_FRAME_H_
#define ZZB_SUBSYS_ZCL_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

/* ZCL header with the manufacturer code */
#define ZZB_ZCL_HDR_MAX_LEN 5

enum zzb_zcl_frame_type {
	ZZB_ZCL_FRAME_GLOBAL = 0,
	ZZB_ZCL_FRAME_CLUSTER = 1,
};

/* zzb_zcl_hdr.flags, at the positions of the frame control field */
#define ZZB_ZCL_MANUFACTURER        BIT(2)
#define ZZB_ZCL_TO_CLIENT           BIT(3)
#define ZZB_ZCL_DISABLE_DEFAULT_RSP BIT(4)

/* The manufacturer code is valid with ZZB_ZCL_MANUFACTURER */
struct zzb_zcl_hdr {
	enum zzb_zcl_frame_type type;
	uint8_t flags;
	uint16_t manufacturer;
	uint8_t tsn;
	uint8_t cmd;
};

/* Returns the header length. */
size_t zzb_zcl_hdr_len(const struct zzb_zcl_hdr *hdr);

/* Writes hdr and returns its length. */
size_t zzb_zcl_hdr_write(const struct zzb_zcl_hdr *hdr, uint8_t *p);

/*
 * Returns the header length, -EINVAL if the header is longer than len, or -ENOTSUP for a reserved
 * frame type. Reserved bits are ignored (ZCL 2.3.2).
 */
int zzb_zcl_hdr_parse(const uint8_t *p, size_t len, struct zzb_zcl_hdr *hdr);

/*
 * Returns the length of the value of type at p, with the length field of a string (ZCL Table
 * 2-11), -EINVAL if the value is longer than len, or -ENOTSUP for a type whose length the codec
 * does not know.
 */
int zzb_zcl_value_len(uint8_t type, const uint8_t *p, size_t len);

/* Whether type is analog (ZCL Table 2-11): integers, floating point and time */
bool zzb_zcl_type_analog(uint8_t type);

#endif /* ZZB_SUBSYS_ZCL_FRAME_H_ */
