/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zzb/zzb.h>

#include "common/node.h"
#include "common/queue.h"
#include "fakes.h"
#include "storage/settings.h"
#include "zcl/zcl.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, zzb_apsde_data_req, struct zzb_node *, struct net_buf *,
		       const struct zzb_apsde_req *);
DEFINE_FAKE_VOID_FUNC(zzb_bdb_identify_end, struct zzb_node *, uint8_t);
DEFINE_FAKE_VOID_FUNC(zzb_bdb_identify_query_rsp, struct zzb_node *, uint8_t, uint16_t, uint8_t);

struct test_values values1;
struct test_values values2;
struct test_values values3;

static const uint8_t product_code[] = {0x03, 0x00, 'Z', 'Z'};

struct zzb_zcl_identify identify;
struct zzb_zcl_scenes scenes;
struct zzb_zcl_on_off on_off;
struct zzb_zcl_on_off on_off3;
struct test_values values4;
struct zzb_zcl_scenes scenes4;
struct recall_calls recall_calls;
struct zzb_zcl_level level10 = {.start_up_default = 0xff};
struct zzb_zcl_scenes scenes11;
struct zzb_zcl_on_off on_off11;
struct zzb_zcl_level level11 = {.start_up_default = 0xff};
struct zzb_zcl_alarms alarms20;
struct zzb_zcl_alarms alarms21;
struct test_values values20;
struct alarm_reset_calls alarm_reset_calls;

struct zzb_zcl_power_config power_config = {
	.mains_voltage = 2300,
	.mains_frequency = 25,
	.battery_voltage = 30,
	.battery_percentage_remaining = 200,
	.battery_manufacturer = "zzb",
	.battery_size = 0x03,
	.battery_ahr_rating = 250,
	.battery_quantity = 2,
	.battery_rated_voltage = 15,
};
struct zzb_zcl_device_temp device_temp = {
	.current_temperature = INT16_MIN,
};
struct zzb_zcl_alarms alarms30;
struct zzb_zcl_ballast ballast = {
	.physical_min_level = PHYSICAL_MIN,
	.physical_max_level = PHYSICAL_MAX,
	.lamp_quantity = 1,
};
struct zzb_zcl_illuminance illuminance;
struct zzb_zcl_illuminance_level illuminance_level;
struct zzb_zcl_occupancy occupancy = {
	.occupancy_sensor_type_bitmap = 0x01,
};
struct zzb_zcl_scenes scenes40;
struct zzb_zcl_on_off on_off40;
struct zzb_zcl_level level40 = {.start_up_default = 0xff};
struct zzb_zcl_color color40 = {
	.color_capabilities = ZZB_ZCL_COLOR_CAP_XY | ZZB_ZCL_COLOR_CAP_TEMPERATURE,
	.color_temp_physical_min = COLOR_TEMP_MIN,
	.color_temp_physical_max = COLOR_TEMP_MAX,
	.couple_color_temp_min = COLOR_COUPLE_MIN,
	.start_up_default = 0xffff,
};
struct zzb_zcl_on_off on_off41;
struct zzb_zcl_level level41 = {.start_up_default = 0xff};
struct zzb_zcl_color color41 = {
	.color_capabilities = ZZB_ZCL_COLOR_CAP_TEMPERATURE,
	.color_temp_physical_min = COLOR_TEMP_MIN,
	.color_temp_physical_max = COLOR_TEMP_MAX,
	.couple_color_temp_min = COLOR_COUPLE_MIN,
	.start_up_default = 0xffff,
};
struct zzb_zcl_scenes scenes50;
struct zzb_zcl_window_covering covering50 = {
	.window_covering_type = 0x08,
};

struct zzb_zcl_basic basic = {
	.app_version = 1,
	.stack_version = 2,
	.hw_version = 3,
	.manufacturer_name = "zzb",
	.model_id = "test",
	.date_code = "20261019",
	.power_source = 0x01,
	.generic_device_class = 0xff,
	.generic_device_type = 0xff,
	.product_code = product_code,
	.product_url = "u",
	.sw_build_id = "1.0",
};
struct cmd_calls cmd_calls;
int cmd_status;
const void *check_values;
struct changed_calls hook_calls;
struct zcl_cb_calls zcl_cb_calls;
struct effect_calls effect_calls;
struct zcl_cmd_calls zcl_cmd_calls;
uint8_t zcl_cmd_status;
bool zcl_cmd_responds;
int zcl_cmd_respond_err;
struct frame_copy sent[8];
struct membership memberships[CONFIG_ZZB_APS_GROUP_COUNT];

/* The membership of group and ep, or a free one with ep 0 */
static struct membership *find_membership(uint16_t group, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->group == group && m->ep == ep) {
			return m;
		}
	}

	return NULL;
}

bool zzb_aps_group_member(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	return ep != 0 && find_membership(group, ep) != NULL;
}

int zzb_apsme_add_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	struct membership *m = find_membership(group, ep);

	if (m == NULL) {
		m = find_membership(0, 0);
	}

	if (m == NULL) {
		return -ENOMEM;
	}

	*m = (struct membership){.group = group, .ep = ep};
	return 0;
}

int zzb_apsme_remove_group(struct zzb_node *node, uint16_t group, uint8_t ep)
{
	struct membership *m = find_membership(group, ep);

	if (m == NULL || ep == 0) {
		return -ENOENT;
	}

	*m = (struct membership){0};
	return 0;
}

void zzb_apsme_remove_all_groups(struct zzb_node *node, uint8_t ep)
{
	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->ep == ep) {
			*m = (struct membership){0};
		}
	}
}

int zzb_aps_group_capacity(struct zzb_node *node)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(memberships, m) {
		count += m->ep == 0;
	}

	return count;
}

int zzb_aps_groups_of(struct zzb_node *node, uint8_t ep, uint16_t *groups, int max)
{
	int count = 0;

	ARRAY_FOR_EACH_PTR(memberships, m) {
		if (m->ep == ep && count < max) {
			groups[count++] = m->group;
		}
	}

	return count;
}

static int test_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd, void *values)
{
	cmd_calls.count++;
	cmd_calls.cmd = *cmd;
	zassert_true(cmd->len <= sizeof(cmd_calls.payload));
	memcpy(cmd_calls.payload, cmd->payload, cmd->len);
	cmd_calls.values = values;
	return cmd_status;
}

static uint8_t test_check(const void *values, const struct zzb_zcl_attr *attr, uint32_t value)
{
	check_values = values;
	return attr->id == 0x0003 && value > TEST_E8_MAX ? ZZB_ZCL_INVALID_VALUE : ZZB_ZCL_SUCCESS;
}

static void test_changed(struct zzb_node *node, uint8_t ep, void *values, uint16_t attr)
{
	zassert_true(hook_calls.count < ARRAY_SIZE(hook_calls.attr));
	zassert_equal_ptr(values, ep == 1 ? &values1 : &values2);
	hook_calls.ep[hook_calls.count] = ep;
	hook_calls.src_ep[hook_calls.count] =
		node->zcl.received != NULL ? node->zcl.received->src_ep : 0;
	hook_calls.attr[hook_calls.count++] = attr;
}

static void test_reset(struct zzb_node *node, uint8_t ep, void *values)
{
	zzb_zcl_set(node, ep, TEST_CLUSTER, 0x0001, 0);
	zzb_zcl_set(node, ep, TEST_CLUSTER, 0x0002, 0);
	zzb_zcl_set(node, ep, TEST_CLUSTER, 0x000f, 0);
}

static void record_zcl_cb(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint16_t attr)
{
	int i = zcl_cb_calls.count++;

	zassert_true(i < ARRAY_SIZE(zcl_cb_calls.attr));
	zcl_cb_calls.ep[i] = ep;
	zcl_cb_calls.cluster[i] = cluster;
	zcl_cb_calls.attr[i] = attr;
}

static void record_effect(struct zzb_node *node, uint8_t ep, uint16_t cluster, uint8_t effect,
			  uint8_t variant)
{
	effect_calls = (struct effect_calls){
		.count = effect_calls.count + 1,
		.ep = ep,
		.cluster = cluster,
		.effect = effect,
		.variant = variant,
		.zcl_cb_count = zcl_cb_calls.count,
	};
}

static uint8_t record_zcl_cmd(struct zzb_node *node, const struct zzb_zcl_cmd *cmd)
{
	int i = zcl_cmd_calls.count++;

	zassert_true(i < ARRAY_SIZE(zcl_cmd_calls.cmd));
	zassert_true(cmd->len <= sizeof(zcl_cmd_calls.payload[i]));
	zcl_cmd_calls.cmd[i] = *cmd;
	memcpy(zcl_cmd_calls.payload[i], cmd->payload, cmd->len);
	if (zcl_cmd_responds) {
		zcl_cmd_respond_err = zzb_zcl_cmd_respond(node, cmd, 0x42, (uint8_t[]){0xaa}, 1);
	}

	return zcl_cmd_status;
}

static const struct zzb_zcl_attr test_attrs[] = {
	ZZB_ZCL_ATTR(0x0000, ZZB_ZCL_BOOL, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT, struct test_values, b),
	ZZB_ZCL_ATTR(0x0001, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT, struct test_values, u8),
	ZZB_ZCL_ATTR(0x0002, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct test_values, u16),
	ZZB_ZCL_ATTR(0x0003, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE, struct test_values, e8),
	ZZB_ZCL_ATTR(0x0004, ZZB_ZCL_MAP8, ZZB_ZCL_WRITE, struct test_values, map8),
	ZZB_ZCL_ATTR(0x0005, ZZB_ZCL_STRING, 0, struct test_values, str),
	ZZB_ZCL_ATTR(0x0006, ZZB_ZCL_OCTSTR, 0, struct test_values, octstr),
	ZZB_ZCL_ATTR(0x0007, ZZB_ZCL_STRING, 0, struct test_values, null_str),
	ZZB_ZCL_ATTR(0x0008, ZZB_ZCL_INT16, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct test_values, i16),
	ZZB_ZCL_ATTR(0x0009, ZZB_ZCL_UINT24, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct test_values, u24),
	ZZB_ZCL_ATTR(0x000a, ZZB_ZCL_MAP32, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct test_values, map32),
	ZZB_ZCL_ATTR(0x000b, ZZB_ZCL_MAP16, 0, struct test_values, map16),
	ZZB_ZCL_ATTR(0x000c, ZZB_ZCL_INT8, ZZB_ZCL_WRITE | ZZB_ZCL_REPORT | ZZB_ZCL_NV,
		     struct test_values, i8),
	ZZB_ZCL_ATTR(0x000d, ZZB_ZCL_UINT32, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct test_values, u32),
	ZZB_ZCL_ATTR(0x000e, ZZB_ZCL_ENUM16, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct test_values, e16),
	ZZB_ZCL_ATTR(0x000f, ZZB_ZCL_EUI64, ZZB_ZCL_WRITE | ZZB_ZCL_NV, struct test_values, eui64),
	ZZB_ZCL_ATTR_CONST(0xfffd, ZZB_ZCL_UINT16, TEST_REVISION),
};

static const struct zzb_zcl_cluster_def test_def = {
	.attrs = test_attrs,
	.attr_count = ARRAY_SIZE(test_attrs),
	.reset = test_reset,
	.cmd = test_cmd,
	.check = test_check,
	.changed = test_changed,
};

static void scene_recall(struct zzb_node *node, uint8_t ep, void *values, const uint32_t *set,
			 size_t count, uint32_t transition_ds)
{
	zassert_true(count <= ARRAY_SIZE(recall_calls.set));
	recall_calls.count++;
	recall_calls.ep = ep;
	memcpy(recall_calls.set, set, count * sizeof(set[0]));
	recall_calls.set_count = count;
	recall_calls.transition_ds = transition_ds;
}

static const struct zzb_zcl_attr scene_test_attrs[] = {
	ZZB_ZCL_ATTR(0x0000, ZZB_ZCL_BOOL, ZZB_ZCL_WRITE, struct test_values, b),
	ZZB_ZCL_ATTR(0x0001, ZZB_ZCL_UINT8, ZZB_ZCL_WRITE, struct test_values, u8),
	ZZB_ZCL_ATTR(0x0002, ZZB_ZCL_UINT16, ZZB_ZCL_WRITE, struct test_values, u16),
	ZZB_ZCL_ATTR(0x0003, ZZB_ZCL_ENUM8, ZZB_ZCL_WRITE, struct test_values, e8),
};

static const struct zzb_zcl_scene_attr scene_test_set[] = {
	{0x0002, ZZB_ZCL_UINT16},
	{0x0000, ZZB_ZCL_BOOL},
	{0x0100, ZZB_ZCL_UINT8},
	{0x0001, ZZB_ZCL_UINT8},
};

static const struct zzb_zcl_cluster_def scene_test_def = {
	.attrs = scene_test_attrs,
	.attr_count = ARRAY_SIZE(scene_test_attrs),
	.scene_attrs = scene_test_set,
	.scene_attr_count = ARRAY_SIZE(scene_test_set),
	.recall = scene_recall,
};

static const struct zzb_zcl_cluster ep1_clusters[] = {
	{.id = TEST_CLUSTER, .server = true, .values = &values1, .def = &test_def},
	{.id = ON_OFF},
	ZZB_ZCL_IDENTIFY_CLIENT(),
};

static const struct zzb_zcl_cluster ep2_clusters[] = {
	{.id = TEST_CLUSTER, .server = true, .values = &values2, .def = &test_def},
	ZZB_ZCL_BASIC_SERVER(&basic),
	ZZB_ZCL_IDENTIFY_SERVER(&identify),
	ZZB_ZCL_GROUPS_SERVER(),
	ZZB_ZCL_SCENES_SERVER(&scenes),
	ZZB_ZCL_ON_OFF_SERVER(&on_off),
};

static const struct zzb_zcl_cluster ep3_clusters[] = {
	{.id = ON_OFF},
	ZZB_ZCL_ON_OFF_SERVER(&on_off3),
	{.id = TEST_CLUSTER, .values = &values3, .def = &test_def},
};

static const struct zzb_zcl_cluster ep5_clusters[] = {
	ZZB_ZCL_GROUPS_CLIENT(),
	ZZB_ZCL_SCENES_CLIENT(),
	ZZB_ZCL_ON_OFF_CLIENT(),
	ZZB_ZCL_LEVEL_CLIENT(),
	ZZB_ZCL_COLOR_CLIENT(),
	ZZB_ZCL_ALARMS_CLIENT(),
	ZZB_ZCL_ILLUMINANCE_CLIENT(),
	ZZB_ZCL_ILLUMINANCE_LEVEL_CLIENT(),
	ZZB_ZCL_OCCUPANCY_CLIENT(),
	ZZB_ZCL_WINDOW_COVERING_CLIENT(),
};

static const struct zzb_zcl_cluster ep4_clusters[] = {
	ZZB_ZCL_SCENES_SERVER(&scenes4),
	{.id = SCENE_CLUSTER, .server = true, .values = &values4, .def = &scene_test_def},
};

static const struct zzb_zcl_cluster ep10_clusters[] = {
	ZZB_ZCL_LEVEL_SERVER(&level10),
};

static const struct zzb_zcl_cluster ep11_clusters[] = {
	ZZB_ZCL_SCENES_SERVER(&scenes11),
	ZZB_ZCL_ON_OFF_SERVER(&on_off11),
	ZZB_ZCL_LEVEL_SERVER(&level11),
};

static void alarm_reset(struct zzb_node *node, uint8_t ep, void *values, uint8_t code)
{
	alarm_reset_calls = (struct alarm_reset_calls){
		.count = alarm_reset_calls.count + 1,
		.ep = ep,
		.values = values,
		.code = code,
	};
}

static const struct zzb_zcl_cluster_def alarm_test_def = {
	.alarm_reset = alarm_reset,
};

static const struct zzb_zcl_cluster ep20_clusters[] = {
	ZZB_ZCL_ALARMS_SERVER(&alarms20),
	{.id = ALARM_CLUSTER, .server = true, .values = &values20, .def = &alarm_test_def},
};

static const struct zzb_zcl_cluster ep21_clusters[] = {
	ZZB_ZCL_ALARMS_SERVER(&alarms21),
};

static const struct zzb_zcl_cluster ep30_clusters[] = {
	ZZB_ZCL_POWER_CONFIG_SERVER(&power_config),
	ZZB_ZCL_DEVICE_TEMP_SERVER(&device_temp),
	ZZB_ZCL_ALARMS_SERVER(&alarms30),
	ZZB_ZCL_BALLAST_SERVER(&ballast),
	ZZB_ZCL_ILLUMINANCE_SERVER(&illuminance),
	ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&illuminance_level),
	ZZB_ZCL_OCCUPANCY_SERVER(&occupancy),
};

static const struct zzb_zcl_cluster ep40_clusters[] = {
	ZZB_ZCL_SCENES_SERVER(&scenes40),
	ZZB_ZCL_ON_OFF_SERVER(&on_off40),
	ZZB_ZCL_LEVEL_SERVER(&level40),
	ZZB_ZCL_COLOR_SERVER(&color40),
};

static const struct zzb_zcl_cluster ep41_clusters[] = {
	ZZB_ZCL_ON_OFF_SERVER(&on_off41),
	ZZB_ZCL_LEVEL_SERVER(&level41),
	ZZB_ZCL_COLOR_SERVER(&color41),
};

static const struct zzb_zcl_cluster ep50_clusters[] = {
	ZZB_ZCL_SCENES_SERVER(&scenes50),
	ZZB_ZCL_WINDOW_COVERING_SERVER(&covering50),
};

struct zzb_zcl_scenes scenes60;
struct zzb_zcl_thermostat thermostat60 = {
	.occupancy = 0x01,
	.abs_min_heat_setpoint_limit = 700,
	.abs_max_heat_setpoint_limit = 3000,
	.abs_min_cool_setpoint_limit = 1600,
	.abs_max_cool_setpoint_limit = 3200,
	.min_setpoint_dead_band = 25,
	.control_sequence_of_operation = ZZB_ZCL_SEQUENCE_COOLING_HEATING,
	.system_mode_default = ZZB_ZCL_SYSTEM_MODE_AUTO,
};

struct zzb_zcl_thermostat_ui thermostat_ui60;

static const struct zzb_zcl_cluster ep60_clusters[] = {
	ZZB_ZCL_SCENES_SERVER(&scenes60),
	ZZB_ZCL_THERMOSTAT_SERVER(&thermostat60),
	ZZB_ZCL_THERMOSTAT_UI_SERVER(&thermostat_ui60),
	ZZB_ZCL_THERMOSTAT_CLIENT(),
};

struct zzb_zcl_temperature temperature70;
struct zzb_zcl_pressure pressure70;
struct zzb_zcl_flow flow70;
struct zzb_zcl_humidity humidity70;
struct zzb_zcl_fan fan70 = {.fan_mode_sequence = 0x02};
struct zzb_zcl_pump pump70;

static const struct zzb_zcl_cluster ep70_clusters[] = {
	ZZB_ZCL_TEMPERATURE_SERVER(&temperature70),
	ZZB_ZCL_PRESSURE_SERVER(&pressure70),
	ZZB_ZCL_FLOW_SERVER(&flow70),
	ZZB_ZCL_HUMIDITY_SERVER(&humidity70),
	ZZB_ZCL_FAN_SERVER(&fan70),
	ZZB_ZCL_PUMP_SERVER(&pump70),
};

static const struct zzb_zcl_cluster ep71_clusters[] = {
	ZZB_ZCL_TEMPERATURE_CLIENT(), ZZB_ZCL_PRESSURE_CLIENT(), ZZB_ZCL_FLOW_CLIENT(),
	ZZB_ZCL_HUMIDITY_CLIENT(),    ZZB_ZCL_FAN_CLIENT(),      ZZB_ZCL_PUMP_CLIENT(),
};

static const struct zzb_endpoint endpoints[] = {
	{
		.id = 1,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep1_clusters,
		.cluster_count = ARRAY_SIZE(ep1_clusters),
	},
	{
		.id = 2,
		.profile = 0x0104,
		.device_id = 0x0100,
		.clusters = ep2_clusters,
		.cluster_count = ARRAY_SIZE(ep2_clusters),
	},
	{
		.id = 3,
		.profile = 0x0104,
		.device_id = 0x0103,
		.clusters = ep3_clusters,
		.cluster_count = ARRAY_SIZE(ep3_clusters),
	},
	{
		.id = 4,
		.profile = 0x0104,
		.device_id = 0xfff0,
		.clusters = ep4_clusters,
		.cluster_count = ARRAY_SIZE(ep4_clusters),
	},
	{
		.id = CLIENT_EP,
		.profile = 0x0104,
		.device_id = 0x0840,
		.clusters = ep5_clusters,
		.cluster_count = ARRAY_SIZE(ep5_clusters),
	},
	{
		.id = 10,
		.profile = 0x0104,
		.device_id = 0x0101,
		.clusters = ep10_clusters,
		.cluster_count = ARRAY_SIZE(ep10_clusters),
	},
	{
		.id = 11,
		.profile = 0x0104,
		.device_id = 0x0101,
		.clusters = ep11_clusters,
		.cluster_count = ARRAY_SIZE(ep11_clusters),
	},
	{
		.id = 20,
		.profile = 0x0104,
		.device_id = 0x0101,
		.clusters = ep20_clusters,
		.cluster_count = ARRAY_SIZE(ep20_clusters),
	},
	{
		.id = 21,
		.profile = 0x0104,
		.device_id = 0x0101,
		.clusters = ep21_clusters,
		.cluster_count = ARRAY_SIZE(ep21_clusters),
	},
	{
		.id = SENSOR_EP,
		.profile = 0x0104,
		.device_id = 0x0108,
		.clusters = ep30_clusters,
		.cluster_count = ARRAY_SIZE(ep30_clusters),
	},
	{
		.id = 40,
		.profile = 0x0104,
		.device_id = 0x010d,
		.clusters = ep40_clusters,
		.cluster_count = ARRAY_SIZE(ep40_clusters),
	},
	{
		.id = 41,
		.profile = 0x0104,
		.device_id = 0x010c,
		.clusters = ep41_clusters,
		.cluster_count = ARRAY_SIZE(ep41_clusters),
	},
	{
		.id = COVERING_EP,
		.profile = 0x0104,
		.device_id = 0x0202,
		.clusters = ep50_clusters,
		.cluster_count = ARRAY_SIZE(ep50_clusters),
	},
	{
		.id = THERMOSTAT_EP,
		.profile = 0x0104,
		.device_id = 0x0301,
		.clusters = ep60_clusters,
		.cluster_count = ARRAY_SIZE(ep60_clusters),
	},
	{
		.id = HVAC_EP,
		.profile = 0x0104,
		.device_id = 0x0303,
		.clusters = ep70_clusters,
		.cluster_count = ARRAY_SIZE(ep70_clusters),
	},
	{
		.id = 71,
		.profile = 0x0104,
		.device_id = 0x0304,
		.clusters = ep71_clusters,
		.cluster_count = ARRAY_SIZE(ep71_clusters),
	},
};

static const struct zzb_device device = {
	.power_source = ZZB_POWER_MAINS,
	.endpoints = endpoints,
	.endpoint_count = ARRAY_SIZE(endpoints),
};

static int apsde_data_req_take(struct zzb_node *node, struct net_buf *buf,
			       const struct zzb_apsde_req *req)
{
	struct frame_copy *copy = &sent[zzb_apsde_data_req_fake.call_count - 1];

	zassert_true(zzb_apsde_data_req_fake.call_count <= ARRAY_SIZE(sent));
	zassert_true(buf->len <= sizeof(copy->data));
	copy->req = *req;
	copy->handle = zzb_frame_meta(buf)->handle;
	memcpy(copy->data, buf->data, buf->len);
	copy->len = buf->len;
	net_buf_unref(buf);
	return 0;
}

struct zzb_node *test_node(void)
{
	static struct zzb_node *node;
	const struct zzb_node_config cfg = {
		.eui64 = NODE_EUI64,
		.device = &device,
		.zcl_cb = record_zcl_cb,
		.zcl_effect_cb = record_effect,
		.zcl_cmd_cb = record_zcl_cmd,
	};

	if (node == NULL) {
		zassert_ok(zzb_node_init(&cfg, &node));
	}

	return node;
}

void reset_fakes(void)
{
	RESET_FAKE(zzb_apsde_data_req);
	RESET_FAKE(zzb_bdb_identify_end);
	RESET_FAKE(zzb_bdb_identify_query_rsp);
	FFF_RESET_HISTORY();

	zzb_apsde_data_req_fake.custom_fake = apsde_data_req_take;
	cmd_calls = (struct cmd_calls){0};
	cmd_status = ZZB_ZCL_SUCCESS;
	check_values = NULL;
	hook_calls = (struct changed_calls){0};
	zcl_cb_calls = (struct zcl_cb_calls){0};
	effect_calls = (struct effect_calls){0};
	zcl_cmd_calls = (struct zcl_cmd_calls){0};
	zcl_cmd_status = ZZB_ZCL_SUCCESS;
	zcl_cmd_responds = false;
	zcl_cmd_respond_err = 0;
	recall_calls = (struct recall_calls){0};
	alarm_reset_calls = (struct alarm_reset_calls){0};
	memset(memberships, 0, sizeof(memberships));
}

void check_no_buffer_leak(void)
{
	struct net_buf *bufs[CONFIG_ZZB_BUF_COUNT];

	ARRAY_FOR_EACH(bufs, i) {
		bufs[i] = zzb_buf_alloc(K_NO_WAIT);
		zassert_not_null(bufs[i], "buffer leak");
	}

	ARRAY_FOR_EACH(bufs, i) {
		net_buf_unref(bufs[i]);
	}
}

void receive_ind(const struct zzb_apsde_ind *ind, const uint8_t *asdu, size_t len)
{
	struct net_buf *buf = zzb_buf_alloc(K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add_mem(buf, asdu, len);
	zzb_zcl_data_ind(test_node(), buf, ind);
}

void receive_ep(uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct zzb_apsde_ind ind = {
		.dst_addr = NODE_ADDR,
		.dst_ep = ep,
		.src_addr = REQUESTER_ADDR,
		.src_ep = REQUESTER_EP,
		.profile = 0x0104,
		.cluster = cluster,
		.security = ZZB_APS_SECURED_NWK_KEY,
	};

	receive_ind(&ind, asdu, len);
}

void receive(uint16_t cluster, const uint8_t *asdu, size_t len)
{
	receive_ep(1, cluster, asdu, len);
}

void check_frame(int i, uint8_t ep, uint16_t cluster, const uint8_t *asdu, size_t len)
{
	const struct frame_copy *copy = &sent[i];

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_SHORT);
	zassert_equal(copy->req.dst_addr, REQUESTER_ADDR);
	zassert_equal(copy->req.dst_ep, REQUESTER_EP);
	zassert_equal(copy->req.src_ep, ep);
	zassert_equal(copy->req.profile, 0x0104);
	zassert_equal(copy->req.cluster, cluster);
	zassert_equal(copy->req.tx_options, ZZB_APS_TX_ACK);
	zassert_equal(copy->handle, ZZB_ZCL_HANDLE);
	zassert_equal(copy->len, len);
	zassert_mem_equal(copy->data, asdu, len);
}

void check_zcl_cb(uint8_t ep, uint16_t cluster, const uint16_t *attrs, int count)
{
	int n = 0;

	for (int i = 0; i < zcl_cb_calls.count; i++) {
		if (zcl_cb_calls.ep[i] != ep || zcl_cb_calls.cluster[i] != cluster) {
			continue;
		}

		zassert_true(n < count);
		zassert_equal(zcl_cb_calls.attr[i], attrs[n++]);
	}

	zassert_equal(n, count);
}

void check_alarm(int i, uint8_t ep, uint16_t cluster, uint8_t code)
{
	const struct frame_copy *copy = &sent[i];
	const uint8_t payload[] = {ZZB_ZCL_ALARMS_ALARM, code, cluster & 0xff, cluster >> 8};

	zassert_true(zzb_apsde_data_req_fake.call_count > i);
	zassert_equal(copy->req.dst_mode, ZZB_APS_ADDR_BINDING);
	zassert_equal(copy->req.src_ep, ep);
	zassert_equal(copy->req.cluster, ZZB_ZCL_ALARMS);
	zassert_equal(copy->len, 2 + sizeof(payload));
	zassert_equal(copy->data[0], 0x19);
	zassert_mem_equal(copy->data + 2, payload, sizeof(payload));
}

int alarm_entries(uint8_t ep, uint16_t cluster, uint8_t code)
{
	int n = 0;

	ARRAY_FOR_EACH_PTR(test_node()->zcl.alarms, a) {
		n += a->in_use && a->ep == ep && a->cluster == cluster && a->code == code;
	}

	return n;
}

void reset_alarm(uint8_t ep, uint16_t cluster, uint8_t code)
{
	const uint8_t req[] = {0x11, 0x70, 0x00, code, cluster & 0xff, cluster >> 8};

	receive_ep(ep, ZZB_ZCL_ALARMS, req, sizeof(req));
}

void reset_all_alarms(uint8_t ep)
{
	const uint8_t req[] = {0x11, 0x71, 0x01};

	receive_ep(ep, ZZB_ZCL_ALARMS, req, sizeof(req));
}

struct reboot_call {
	struct k_work work;
	int err;
};

static void reboot_handler(struct k_work *work)
{
	struct reboot_call *call = CONTAINER_OF(work, struct reboot_call, work);

	zzb_zcl_init(test_node());
	call->err = zzb_settings_load(test_node());
	if (call->err == 0) {
		zzb_zcl_start(test_node());
	}
}

int reboot(void)
{
	struct reboot_call call = {0};

	zzb_call_sync(&call.work, reboot_handler);
	return call.err;
}

static void reset_handler(struct k_work *work)
{
	zzb_zcl_reset(test_node());
}

void reset_node(void)
{
	struct k_work work;

	zzb_call_sync(&work, reset_handler);
}
