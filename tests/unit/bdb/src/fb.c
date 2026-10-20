/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zzb/bdb.h>

#include "bdb/bdb.h"
#include "fakes.h"
#include "zdo/zdo.h"

static struct zzb_node *node;

static void *setup(void)
{
	node = test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
	reboot(true);
}

static void check_event(enum zzb_bdb_event event, enum zzb_bdb_status status)
{
	zassert_equal(events.count, 1);
	zassert_equal(events.event, event);
	zassert_equal(events.status, status);
}

/* BDB 8.4 step 1 */
ZTEST(zzb_bdb_fb, test_target_off_a_network)
{
	reboot(false);
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_NOT_ON_A_NETWORK);
	zassert_equal(zzb_zcl_identify_fake.call_count, 0);
}

/* BDB 8.4 steps 3 to 5: the endpoint identifies for bdbcMinCommissioningTime */
ZTEST(zzb_bdb_fb, test_target)
{
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	zassert_equal(zzb_zcl_identify_fake.call_count, 1);
	zassert_equal(zzb_zcl_identify_fake.arg1_val, IDENTIFY_EP);
	zassert_equal(zzb_zcl_identify_fake.arg2_val, 180);
	zassert_equal(zzb_bdb_finding_binding_target(node, IDENTIFY_EP), -EBUSY);

	/* Steering and the other procedures stay available */
	zassert_ok(zzb_bdb_steering(node));
	events.count = 0;

	zzb_bdb_identify_end(node, OTHER_EP);
	zassert_equal(events.count, 0);
	zzb_bdb_identify_end(node, IDENTIFY_EP);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_TARGET, ZZB_BDB_SUCCESS);

	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
}

ZTEST(zzb_bdb_fb, test_target_without_an_identify_server)
{
	zassert_equal(zzb_bdb_finding_binding_target(node, OTHER_EP), -ENOENT);
	zassert_equal(zzb_bdb_finding_binding_target(node, 9), -ENOENT);
	zassert_equal(zzb_zcl_identify_fake.call_count, 0);
}

static void identify_ends(struct zzb_node *n)
{
	zzb_bdb_identify_end(n, IDENTIFY_EP);
}

/* The reset of the node, whose reset of IdentifyTime reaches BDB, ends the target without event */
ZTEST(zzb_bdb_fb, test_target_and_reset)
{
	zzb_zcl_reset_fake.custom_fake = identify_ends;
	zassert_ok(zzb_bdb_finding_binding_target(node, IDENTIFY_EP));
	zzb_nlme_leave_ind(node, 0, false);
	check_event(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

#define RESPONDENT       0x1234
#define RESPONDENT_EUI64 0x00124b0000001234ULL

/* Simple_Desc_rsp of an On/Off Light with a Temperature Measurement client */
static const uint8_t light_in[] = {0x03, 0x00, 0x06, 0x00};
static const uint8_t light_out[] = {0x02, 0x04};

static void simple_desc(uint8_t ep, uint16_t profile)
{
	const struct zzb_zdo_simple_desc desc = {
		.ep = ep,
		.profile = profile,
		.in_count = 2,
		.in = light_in,
		.out_count = 1,
		.out = light_out,
	};

	zzb_zdo_simple_desc_conf(node, ZZB_ZDP_SUCCESS, RESPONDENT, &desc);
}

/* BDB 8.5 steps 3 and 4: the query, then the responses for 5 s */
static void query(uint16_t group)
{
	zassert_ok(zzb_bdb_finding_binding_initiator(node, OTHER_EP, group));
	zassert_equal(zzb_zcl_send_cmd_fake.call_count, 1);
	zassert_equal(zzb_zcl_send_cmd_fake.arg1_val, OTHER_EP);
	zassert_equal(zzb_zcl_send_cmd_fake.arg2_val, 0xffff);
	zassert_equal(zzb_zcl_send_cmd_fake.arg3_val, 0xff);
	zassert_equal(zzb_zcl_send_cmd_fake.arg4_val, 0x0003);
	zassert_equal(zzb_zcl_send_cmd_fake.arg5_val, 0x01);
}

static void check_binding(int i, uint16_t cluster, uint8_t dst_ep)
{
	zassert_equal(bindings[i].src_ep, OTHER_EP);
	zassert_equal(bindings[i].cluster, cluster);
	zassert_equal(bindings[i].dst_mode, ZZB_APS_ADDR_EXT);
	zassert_equal(bindings[i].dst_ext, RESPONDENT_EUI64);
	zassert_equal(bindings[i].dst_ep, dst_ep);
}

/* BDB 8.5 step 1 */
ZTEST(zzb_bdb_fb, test_initiator_off_a_network)
{
	reboot(false);
	zassert_ok(zzb_bdb_finding_binding_initiator(node, OTHER_EP, 0xffff));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_NOT_ON_A_NETWORK);
	zassert_equal(zzb_zcl_send_cmd_fake.call_count, 0);
}

ZTEST(zzb_bdb_fb, test_initiator_rejected)
{
	zassert_equal(zzb_bdb_finding_binding_initiator(node, OTHER_EP, 0x0000), -EINVAL);
	zassert_equal(zzb_bdb_finding_binding_initiator(node, OTHER_EP, 0xfff8), -EINVAL);
	zassert_equal(zzb_bdb_finding_binding_initiator(node, IDENTIFY_EP, 0xffff), -ENOENT);
	query(0xffff);
	zassert_equal(zzb_bdb_finding_binding_initiator(node, OTHER_EP, 0xffff), -EBUSY);
}

ZTEST(zzb_bdb_fb, test_initiator_without_respondents)
{
	query(0xffff);
	k_sleep(K_MSEC(4900));
	zassert_equal(events.count, 0);
	k_sleep(K_MSEC(200));
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_NO_IDENTIFY_QUERY_RESPONSE);
}

/*
 * BDB 8.5 steps 5 to 11: the On/Off client binds to the input cluster of the respondent and the
 * Temperature Measurement server to its output cluster; Identify is not bound. The IEEE address of
 * a respondent is requested once.
 */
ZTEST(zzb_bdb_fb, test_initiator)
{
	query(0xffff);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 1);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 1);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 2);
	/* To another endpoint */
	zzb_bdb_identify_query_rsp(node, IDENTIFY_EP, 0x5555, 1);
	k_sleep(K_MSEC(5100));

	zassert_equal(zzb_zdo_simple_desc_req_fake.call_count, 1);
	zassert_equal(zzb_zdo_simple_desc_req_fake.arg1_val, RESPONDENT);
	zassert_equal(zzb_zdo_simple_desc_req_fake.arg2_val, 1);
	simple_desc(1, 0x0104);
	zassert_equal(zzb_zdo_ieee_addr_req_fake.call_count, 1);
	zassert_equal(zzb_zdo_ieee_addr_req_fake.arg1_val, RESPONDENT);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 0);

	zzb_zdo_ieee_addr_conf(node, ZZB_ZDP_SUCCESS, RESPONDENT, RESPONDENT_EUI64);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 2);
	check_binding(0, 0x0006, 1);
	check_binding(1, 0x0402, 1);

	zassert_equal(zzb_zdo_simple_desc_req_fake.call_count, 2);
	zassert_equal(zzb_zdo_simple_desc_req_fake.arg2_val, 2);
	simple_desc(2, 0x0104);
	zassert_equal(zzb_zdo_ieee_addr_req_fake.call_count, 1);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 4);
	check_binding(2, 0x0006, 2);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
	zassert_equal(zzb_zcl_send_cmd_fake.call_count, 1);
}

/* BDB 8.5 step 6, and respondents whose IEEE address or profile does not fit */
ZTEST(zzb_bdb_fb, test_initiator_skips_respondents)
{
	query(0xffff);
	for (int i = 1; i <= 4; i++) {
		zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, i);
	}

	zzb_zdo_simple_desc_req_fake.return_val = -ENOBUFS;
	k_sleep(K_MSEC(5100));
	zassert_equal(zzb_zdo_simple_desc_req_fake.call_count, 4);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);

	events.count = 0;
	zzb_zdo_simple_desc_req_fake.return_val = 0;
	zzb_zcl_send_cmd_fake.call_count = 0;
	query(0xffff);
	for (int i = 1; i <= 3; i++) {
		zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, i);
	}

	k_sleep(K_MSEC(5100));
	zzb_zdo_simple_desc_conf(node, ZZB_ZDP_TIMEOUT, RESPONDENT, NULL);
	simple_desc(2, 0xc05e);
	simple_desc(3, 0x0104);
	zzb_zdo_ieee_addr_conf(node, ZZB_ZDP_TIMEOUT, RESPONDENT, 0);
	zassert_equal(zzb_zdo_simple_desc_req_fake.call_count, 7);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 0);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
}

/* DTL 43.3.1, 44.3.1, 45.3.1, 46.3.1: IAS Zone, IAS ACE and IAS WD are not bound */
ZTEST(zzb_bdb_fb, test_initiator_skips_ias_clusters)
{
	static const uint8_t ias_in[] = {0x00, 0x05, 0x02, 0x05};
	static const uint8_t ias_out[] = {0x01, 0x05};
	const struct zzb_zdo_simple_desc desc = {
		.ep = 1,
		.profile = 0x0104,
		.in_count = 2,
		.in = ias_in,
		.out_count = 1,
		.out = ias_out,
	};

	query(0xffff);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 1);
	k_sleep(K_MSEC(5100));
	zzb_zdo_simple_desc_conf(node, ZZB_ZDP_SUCCESS, RESPONDENT, &desc);
	zassert_equal(zzb_zdo_ieee_addr_req_fake.call_count, 0);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 0);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
}

/* BDB 8.5 step 8 */
ZTEST(zzb_bdb_fb, test_initiator_binding_table_full)
{
	zzb_apsme_bind_req_fake.custom_fake = NULL;
	zzb_apsme_bind_req_fake.return_val = -ENOMEM;
	query(0xffff);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 1);
	k_sleep(K_MSEC(5100));
	simple_desc(1, 0x0104);
	zzb_zdo_ieee_addr_conf(node, ZZB_ZDP_SUCCESS, RESPONDENT, RESPONDENT_EUI64);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 1);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_BINDING_TABLE_FULL);
}

/* BDB 8.5 steps 8 and 10: group bindings, then Add Group If Identifying as broadcast */
ZTEST(zzb_bdb_fb, test_initiator_group)
{
	query(0x0042);
	zzb_bdb_identify_query_rsp(node, OTHER_EP, RESPONDENT, 1);
	k_sleep(K_MSEC(5100));
	simple_desc(1, 0x0104);
	zassert_equal(zzb_zdo_ieee_addr_req_fake.call_count, 0);
	zassert_equal(zzb_apsme_bind_req_fake.call_count, 2);
	zassert_equal(bindings[0].dst_mode, ZZB_APS_ADDR_GROUP);
	zassert_equal(bindings[0].dst_addr, 0x0042);
	zassert_equal(bindings[0].cluster, 0x0006);

	zassert_equal(zzb_zcl_send_cmd_fake.call_count, 2);
	zassert_equal(zzb_zcl_send_cmd_fake.arg2_val, 0xffff);
	zassert_equal(zzb_zcl_send_cmd_fake.arg3_val, 0xff);
	zassert_equal(zzb_zcl_send_cmd_fake.arg4_val, 0x0004);
	zassert_equal(zzb_zcl_send_cmd_fake.arg5_val, 0x05);
	zassert_equal(zzb_zcl_send_cmd_fake.arg7_val, 3);
	zassert_mem_equal(sent_cmd, ((uint8_t[]){0x42, 0x00, 0x00}), 3);
	check_event(ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR, ZZB_BDB_SUCCESS);
}

/* A parent link failure during the procedure starts the rejoin series */
ZTEST(zzb_bdb_fb, test_initiator_does_not_block_a_rejoin)
{
	query(0xffff);
	zzb_nlme_nwk_status_ind(node, ZZB_NWK_CODE_PARENT_LINK_FAILURE, 0x0000);
	k_sleep(K_MSEC(10));
	zassert_equal(zzb_nlme_join_req_fake.call_count, 1);
}

/* The reset of the node ends the procedure without an event */
ZTEST(zzb_bdb_fb, test_initiator_and_reset)
{
	query(0xffff);
	zzb_nlme_leave_ind(node, 0, false);
	k_sleep(K_MSEC(5100));
	check_event(ZZB_BDB_EVENT_LEFT, ZZB_BDB_SUCCESS);
}

ZTEST_SUITE(zzb_bdb_fb, NULL, setup, before, NULL, NULL);
