/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_H_
#define ZZB_INCLUDE_ZZB_ZCL_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Data types of the attributes of the stack (ZCL Table 2-11). */
enum zzb_zcl_type {
	ZZB_ZCL_BOOL = 0x10,
	ZZB_ZCL_MAP8 = 0x18,
	ZZB_ZCL_UINT8 = 0x20,
	ZZB_ZCL_UINT16 = 0x21,
	ZZB_ZCL_ENUM8 = 0x30,
	ZZB_ZCL_OCTSTR = 0x41,
	ZZB_ZCL_STRING = 0x42,
};

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_H_ */
