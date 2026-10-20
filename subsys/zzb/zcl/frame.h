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

/* Returns the length of a type of fixed length (ZCL Table 2-11), or -ENOTSUP */
int zzb_zcl_type_len(uint8_t type);

/*
 * A field of a command: len octets, or with count_mask a count, the bits count_mask of one octet,
 * followed by that many items of len octets. A character string is counted with items of one octet.
 */
struct zzb_zcl_field {
	uint8_t len;
	uint8_t count_mask;
};

/*
 * Returns the length of the fields at p, which end after count fields or at the first field with
 * len 0, or -EINVAL if they are longer than len.
 */
int zzb_zcl_fields_len(const uint8_t *p, size_t len, const struct zzb_zcl_field *fields,
		       size_t count);

/* The little-endian value of an integer type of up to 8 octets at p; an int24 has 32 bits */
uint64_t zzb_zcl_value_get(uint8_t type, const uint8_t *p);

/* Writes value in the length of an integer type of up to 8 octets to p */
void zzb_zcl_value_put(uint8_t type, uint64_t value, uint8_t *p);

#endif /* ZZB_SUBSYS_ZCL_FRAME_H_ */
