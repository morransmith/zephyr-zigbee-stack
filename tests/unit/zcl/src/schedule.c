/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/thermostat.h>

#include "fakes.h"

/* The payloads fill a day of 4 transitions, the default capacity */
BUILD_ASSERT(CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS == 4);

/* The values that the application set on endpoint 60 */
static struct zzb_zcl_thermostat factory;

static void *setup(void)
{
	test_node();
	factory = thermostat60;
	return NULL;
}

static void before(void *fixture)
{
	thermostat60 = factory;
	reset_node();
	reset_fakes();
}

static void after(void *fixture)
{
	check_no_buffer_leak();
}

static uint8_t tsn;

/*
 * Sends the Thermostat command id with the payload to endpoint 60 after reset_fakes(), and returns
 * the status of its Default Response
 */
static uint8_t command(uint8_t id, const uint8_t *payload, size_t len)
{
	uint8_t frame[32] = {0x01, ++tsn, id};

	zassert_true(3 + len <= sizeof(frame));
	memcpy(frame + 3, payload, len);
	reset_fakes();
	receive_ep(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, frame, 3 + len);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_mem_equal(sent[0].data, ((uint8_t[]){0x18, tsn, 0x0b, id}), 4);
	return sent[0].data[4];
}

static uint8_t set_schedule(const uint8_t *payload, size_t len)
{
	return command(ZZB_ZCL_THERMOSTAT_SET_WEEKLY_SCHEDULE, payload, len);
}

/* Checks the mode and the count transitions t of day i of endpoint 60 */
static void check_day(int i, uint8_t mode, const struct zzb_zcl_schedule_transition *t, int count)
{
	const struct zzb_zcl_schedule_day *day = &thermostat60.schedule[i];

	zassert_equal(day->mode, mode, "day %d", i);
	zassert_equal(day->count, count, "day %d", i);
	zassert_mem_equal(day->transitions, t, count * sizeof(*t), "day %d", i);
}

static void check_empty(int i)
{
	zassert_equal(thermostat60.schedule[i].mode, 0, "day %d", i);
	zassert_equal(thermostat60.schedule[i].count, 0, "day %d", i);
}

/*
 * ZCL 6.3.2.3.2: every day of the bitmap gets the sequence in place of its previous one, with the
 * setpoints of its mode. The first command sets Monday and Wednesday to heat at 6:00 to 20 degrees
 * Celsius and at 22:00 to 16, the second Saturday to heat and cool at 8:00 to 18 and 27, the third
 * Away to cool at 0:00 to 30.
 */
ZTEST(zzb_zcl_schedule, test_set)
{
	static const uint8_t heat[] = {0x02, 0x0a, 0x01, 0x68, 0x01, 0xd0,
				       0x07, 0x28, 0x05, 0x40, 0x06};
	static const uint8_t both[] = {0x01, 0x40, 0x03, 0xe0, 0x01, 0x08, 0x07, 0x8c, 0x0a};
	static const uint8_t cool[] = {0x01, 0x80, 0x02, 0x00, 0x00, 0xb8, 0x0b};
	static const struct zzb_zcl_schedule_transition heat_t[] = {
		{360, 2000, INT16_MIN},
		{1320, 1600, INT16_MIN},
	};
	static const struct zzb_zcl_schedule_transition both_t[] = {{480, 1800, 2700}};
	static const struct zzb_zcl_schedule_transition cool_t[] = {{0, INT16_MIN, 3000}};

	zassert_equal(set_schedule(heat, sizeof(heat)), ZZB_ZCL_SUCCESS);
	zassert_equal(set_schedule(both, sizeof(both)), ZZB_ZCL_SUCCESS);
	zassert_equal(set_schedule(cool, sizeof(cool)), ZZB_ZCL_SUCCESS);
	check_empty(0);
	check_day(1, ZZB_ZCL_SCHEDULE_HEAT, heat_t, 2);
	check_empty(2);
	check_day(3, ZZB_ZCL_SCHEDULE_HEAT, heat_t, 2);
	check_empty(4);
	check_empty(5);
	check_day(6, ZZB_ZCL_SCHEDULE_HEAT | ZZB_ZCL_SCHEDULE_COOL, both_t, 1);
	check_day(7, ZZB_ZCL_SCHEDULE_COOL, cool_t, 1);

	/* Monday gets the sequence of Away, and 0 transitions empty Wednesday */
	zassert_equal(set_schedule((uint8_t[]){0x01, 0x02, 0x02, 0x00, 0x00, 0xb8, 0x0b}, 7),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(set_schedule((uint8_t[]){0x00, 0x08, 0x01}, 3), ZZB_ZCL_SUCCESS);
	check_day(1, ZZB_ZCL_SCHEDULE_COOL, cool_t, 1);
	check_empty(3);
	check_day(7, ZZB_ZCL_SCHEDULE_COOL, cool_t, 1);
}

/*
 * Section 16: the checks run in the order MALFORMED_COMMAND for the header, INVALID_FIELD (mode 0,
 * a reserved mode bit alone, no day), INSUFFICIENT_SPACE (5 transitions), MALFORMED_COMMAND for the
 * transitions, INVALID_VALUE (heating setpoints 699, 3001 and 3100, cooling setpoints 1599 and
 * 3201, time 1440) and FAILURE (two transitions at 6:00), and after an error Monday keeps its
 * sequence. The cases are the length, the payload and the status.
 */
ZTEST(zzb_zcl_schedule, test_set_errors)
{
	static const uint8_t monday[] = {0x01, 0x02, 0x01, 0x68, 0x01, 0xd0, 0x07};
	static const struct zzb_zcl_schedule_transition monday_t[] = {{360, 2000, INT16_MIN}};
	static const struct {
		uint8_t len;
		uint8_t payload[16];
		uint8_t status;
	} cases[] = {
		{2, {0x01, 0x02}, 0x80},
		{3, {0x05, 0x02, 0x00}, 0x85},
		{3, {0x05, 0x02, 0x04}, 0x85},
		{3, {0x05, 0x00, 0x01}, 0x85},
		{3, {0x05, 0x02, 0x01}, 0x89},
		{7, {0x02, 0x02, 0x01, 0x68, 0x01, 0xd0, 0x07}, 0x80},
		{8, {0x01, 0x02, 0x03, 0x68, 0x01, 0xd0, 0x07, 0x8c}, 0x80},
		{7, {0x01, 0x02, 0x01, 0x68, 0x01, 0xbb, 0x02}, 0x87},
		{7, {0x01, 0x02, 0x01, 0x68, 0x01, 0xb9, 0x0b}, 0x87},
		{9, {0x01, 0x02, 0x03, 0x68, 0x01, 0x1c, 0x0c, 0x80, 0x0c}, 0x87},
		{7, {0x01, 0x02, 0x02, 0x68, 0x01, 0x3f, 0x06}, 0x87},
		{7, {0x01, 0x02, 0x02, 0x68, 0x01, 0x81, 0x0c}, 0x87},
		{7, {0x01, 0x02, 0x01, 0xa0, 0x05, 0xd0, 0x07}, 0x87},
		{15,
		 {0x03, 0x02, 0x01, 0x68, 0x01, 0xd0, 0x07, 0x68, 0x01, 0xd0, 0x07, 0xa0, 0x05,
		  0xd0, 0x07},
		 0x87},
		{11, {0x02, 0x02, 0x01, 0x68, 0x01, 0xd0, 0x07, 0x68, 0x01, 0xd0, 0x07}, 0x01},
	};

	zassert_equal(set_schedule(monday, sizeof(monday)), ZZB_ZCL_SUCCESS);
	ARRAY_FOR_EACH(cases, i) {
		zassert_equal(set_schedule(cases[i].payload, cases[i].len), cases[i].status,
			      "case %zu", i);
	}

	check_day(1, ZZB_ZCL_SCHEDULE_HEAT, monday_t, 1);
}

/*
 * The limits of a setpoint are the absolute limits of its kind, the dead band does not apply, and
 * the times run from 0 to 1439. The command fills Monday with 4 transitions, the capacity.
 */
ZTEST(zzb_zcl_schedule, test_set_limits)
{
	static const uint8_t req[] = {0x04, 0x02, 0x03, 0x9f, 0x05, 0xbc, 0x02, 0x40, 0x06,
				      0x00, 0x00, 0xb8, 0x0b, 0x80, 0x0c, 0x3c, 0x00, 0xd0,
				      0x07, 0x40, 0x06, 0x78, 0x00, 0xbc, 0x02, 0x80, 0x0c};
	static const struct zzb_zcl_schedule_transition t[] = {
		{1439, 700, 1600},
		{0, 3000, 3200},
		{60, 2000, 1600},
		{120, 700, 3200},
	};

	zassert_equal(set_schedule(req, sizeof(req)), ZZB_ZCL_SUCCESS);
	check_day(1, ZZB_ZCL_SCHEDULE_HEAT | ZZB_ZCL_SCHEDULE_COOL, t, ARRAY_SIZE(t));
}

ZTEST_SUITE(zzb_zcl_schedule, NULL, setup, before, after, NULL);
