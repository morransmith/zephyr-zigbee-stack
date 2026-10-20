/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_
#define ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_

#include <stdint.h>
#include <zzb/zcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IAS Zone cluster (ZCL 8.2). */
#define ZZB_ZCL_IAS_ZONE 0x0500

/** Attributes (ZCL Tables 8-3 and 8-8). */
#define ZZB_ZCL_IAS_ZONE_STATE                                  0x0000
#define ZZB_ZCL_IAS_ZONE_TYPE                                   0x0001
#define ZZB_ZCL_IAS_ZONE_STATUS                                 0x0002
#define ZZB_ZCL_IAS_ZONE_CIE_ADDRESS                            0x0010
#define ZZB_ZCL_IAS_ZONE_ID                                     0x0011
#define ZZB_ZCL_IAS_ZONE_NUMBER_OF_SENSITIVITY_LEVELS_SUPPORTED 0x0012
#define ZZB_ZCL_IAS_ZONE_CURRENT_SENSITIVITY_LEVEL              0x0013

/** Bits of ZoneStatus (ZCL Table 8-6). */
#define ZZB_ZCL_IAS_ZONE_STATUS_ALARM1         BIT(0)
#define ZZB_ZCL_IAS_ZONE_STATUS_ALARM2         BIT(1)
#define ZZB_ZCL_IAS_ZONE_STATUS_TAMPER         BIT(2)
#define ZZB_ZCL_IAS_ZONE_STATUS_BATTERY        BIT(3)
#define ZZB_ZCL_IAS_ZONE_STATUS_SUPERVISION    BIT(4)
#define ZZB_ZCL_IAS_ZONE_STATUS_RESTORE        BIT(5)
#define ZZB_ZCL_IAS_ZONE_STATUS_TROUBLE        BIT(6)
#define ZZB_ZCL_IAS_ZONE_STATUS_AC             BIT(7)
#define ZZB_ZCL_IAS_ZONE_STATUS_TEST           BIT(8)
#define ZZB_ZCL_IAS_ZONE_STATUS_BATTERY_DEFECT BIT(9)

/** Commands that the server receives (ZCL Table 8-9). */
#define ZZB_ZCL_IAS_ZONE_ENROLL_RSP           0x00
#define ZZB_ZCL_IAS_ZONE_INITIATE_NORMAL_MODE 0x01
#define ZZB_ZCL_IAS_ZONE_INITIATE_TEST_MODE   0x02

/** Commands that the server generates (ZCL Table 8-11). */
#define ZZB_ZCL_IAS_ZONE_STATUS_CHANGE_NOTIFICATION 0x00
#define ZZB_ZCL_IAS_ZONE_ENROLL_REQ                 0x01

/**
 * Values of an IAS Zone server (ZCL 8.2.2.1). The application fills zone_type and
 * number_of_zone_sensitivity_levels_supported before zzb_node_init() and changes zone_status with
 * zzb_zcl_attr_set(), which sends the change to the CIE; these are read-only. The stack keeps the
 * enrollment and current_zone_sensitivity_level, which is writable, and saves them: the CIE writes
 * ias_cie_address and enrolls the zone with Zone Enroll Response. The CIE starts the test mode with
 * Initiate Test Mode; the stack keeps bit 8 (Test) of zone_status, and the application learns the
 * mode from zcl_cb of ZoneStatus.
 */
struct zzb_zcl_ias_zone {
	/** ZoneState: 0x00 not enrolled, 0x01 enrolled. */
	uint8_t zone_state;
	/** ZCL Table 8-5. */
	uint16_t zone_type;
	/** ZCL Table 8-6. */
	uint16_t zone_status;
	/** IAS_CIE_Address: the IEEE address of the CIE; 0 and all ones mean none. */
	uint64_t ias_cie_address;
	/** ZoneID: the index of the zone in the zone table of the CIE, 0xff before enrollment. */
	uint8_t zone_id;
	/** 2 to 0xff; 0 takes the ZCL default 2 in zzb_node_init(). */
	uint8_t number_of_zone_sensitivity_levels_supported;
	/** 0, the default of the device, to number_of_zone_sensitivity_levels_supported. */
	uint8_t current_zone_sensitivity_level;
	/** Internal: the endpoint of the CIE, from which it wrote ias_cie_address. */
	uint8_t cie_ep;
	/** Internal: k_uptime_get() at the first change of zone_status not sent. */
	int64_t since;
	/** Internal: k_uptime_get() at which a refused notification is tried again, or 0. */
	int64_t retry;
	/** Internal: k_uptime_get() at the end of the test mode, 0 outside it. */
	int64_t test_end;
};

extern const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_def;
extern const struct zzb_zcl_cluster_def zzb_zcl_ias_zone_client_def;

/** An IAS Zone server with the values v, a pointer to struct zzb_zcl_ias_zone. */
#define ZZB_ZCL_IAS_ZONE_SERVER(v)                                                                 \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ZONE,                                                            \
		.server = true,                                                                    \
		.values = (1 ? (v) : (struct zzb_zcl_ias_zone *)NULL),                             \
		.def = &zzb_zcl_ias_zone_def,                                                      \
	}

/**
 * An IAS Zone client, which the IAS CIE has (DTL). Zone Status Change Notification and Zone Enroll
 * Request go to zcl_cmd_cb; the application answers Zone Enroll Request with Zone Enroll Response
 * through zzb_zcl_cmd_respond().
 */
#define ZZB_ZCL_IAS_ZONE_CLIENT()                                                                  \
	{                                                                                          \
		.id = ZZB_ZCL_IAS_ZONE,                                                            \
		.server = false,                                                                   \
		.def = &zzb_zcl_ias_zone_client_def,                                               \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZZB_INCLUDE_ZZB_ZCL_IAS_ZONE_H_ */
