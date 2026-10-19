/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/ztest.h>

#include "common/node.h"
#include "fakes.h"
#include "zcl/zcl.h"

static void *setup(void)
{
	test_node();
	return NULL;
}

static void before(void *fixture)
{
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

/* Off With Effect of the On/Off client on endpoint 1, which has no definition */
ZTEST(zzb_zcl_client, test_send_bound)
{
	uint8_t tsn = test_node()->zcl.tsn;

	zassert_ok(zzb_zcl_send_bound(test_node(), 1, ON_OFF, 0x40, (uint8_t[]){0x00, 0x01}, 2));
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(sent[0].req.src_ep, 1);
	zassert_equal(sent[0].req.profile, 0x0104);
	zassert_equal(sent[0].req.cluster, ON_OFF);
	zassert_equal(sent[0].req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(sent[0].handle, ZZB_ZCL_HANDLE);
	zassert_equal(sent[0].len, 5);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x11, tsn, 0x40, 0x00, 0x01}), 5);
}

/* Endpoint 2 has only the On/Off server, endpoint 1 no instance of cluster 0x0008 */
ZTEST(zzb_zcl_client, test_send_bound_without_client)
{
	zassert_equal(zzb_zcl_send_bound(test_node(), 2, ON_OFF, 0x02, NULL, 0), -ENOENT);
	zassert_equal(zzb_zcl_send_bound(test_node(), 1, 0x0008, 0x02, NULL, 0), -ENOENT);
	zassert_equal(zzb_zcl_send_bound(test_node(), 9, ON_OFF, 0x02, NULL, 0), -ENOENT);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 0);
}

/* An error of APSDE-DATA.request, such as off a network, is returned */
ZTEST(zzb_zcl_client, test_send_bound_error)
{
	zzb_apsde_data_req_fake.custom_fake = NULL;
	zzb_apsde_data_req_fake.return_val = -ENOTCONN;
	zassert_equal(zzb_zcl_send_bound(test_node(), 1, ON_OFF, 0x02, NULL, 0), -ENOTCONN);
}

/* The DTL requires ClusterRevision on every client cluster */
ZTEST(zzb_zcl_client, test_client_revisions)
{
	static const struct {
		uint16_t cluster;
		uint8_t revision;
	} clients[] = {
		{0x0004, 3}, {0x0005, 3}, {0x0006, 2}, {0x0008, 2}, {0x0009, 1},
		{0x0300, 3}, {0x0400, 2}, {0x0401, 1}, {0x0406, 2},
	};

	ARRAY_FOR_EACH(clients, i) {
		const uint8_t rsp[] = {
			0x10, 0x70, 0x01, 0xfd, 0xff, 0x00, 0x21, clients[i].revision, 0x00};

		reset_fakes();
		receive_ep(CLIENT_EP, clients[i].cluster, (uint8_t[]){0x08, 0x70, 0x00, 0xfd, 0xff},
			   5);
		check_frame(0, CLIENT_EP, clients[i].cluster, rsp, sizeof(rsp));
	}
}

/* A client accepts a response, such as Add Scene Response, and a Report Attributes */
ZTEST(zzb_zcl_client, test_client_accepts)
{
	static const uint8_t add_scene_rsp[] = {0x09, 0x71, 0x00, 0x00, 0x01, 0x00, 0x05};
	static const uint8_t report[] = {0x08, 0x72, 0x0a, 0x00, 0x00, 0x18, 0x01};

	receive_ep(CLIENT_EP, 0x0005, add_scene_rsp, sizeof(add_scene_rsp));
	check_frame(0, CLIENT_EP, 0x0005, (uint8_t[]){0x10, 0x71, 0x0b, 0x00, 0x00}, 5);

	receive_ep(CLIENT_EP, 0x0406, report, sizeof(report));
	check_frame(1, CLIENT_EP, 0x0406, (uint8_t[]){0x10, 0x72, 0x0b, 0x0a, 0x00}, 5);

	/* Also a client instance without a definition */
	receive_ep(1, ON_OFF, (uint8_t[]){0x08, 0x73, 0x0a, 0x00, 0x00, 0x10, 0x01}, 7);
	check_frame(2, 1, ON_OFF, (uint8_t[]){0x10, 0x73, 0x0b, 0x0a, 0x00}, 5);
}

ZTEST_SUITE(zzb_zcl_client, NULL, setup, before, after, NULL);
