/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zzb/zcl/thermostat.h>

#include "fakes.h"

/* The values that the application set on endpoint 60 */
static struct zzb_zcl_thermostat factory;

static void *setup(void)
{
	test_node();
	factory = thermostat60;
	return NULL;
}

/* Each test starts with the values of the application, the defaults and without records */
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
 * Writes value of type to attribute attr of endpoint 60 after reset_fakes(), and returns the
 * status of the response
 */
static uint8_t write(uint16_t attr, uint8_t type, uint16_t value)
{
	uint8_t req[8] = {0x00, ++tsn, 0x02};

	sys_put_le16(attr, req + 3);
	req[5] = type;
	sys_put_le16(value, req + 6);
	reset_fakes();
	receive_ep(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, req, ZZB_ZCL_IS_BYTE(type) ? 7 : 8);
	zassert_equal(zzb_apsde_data_req_fake.call_count, 1);
	zassert_equal(sent[0].data[1], tsn);
	return sent[0].data[3];
}

static uint8_t write_temperature(uint16_t attr, int16_t t)
{
	return write(attr, ZZB_ZCL_INT16, t);
}

static uint8_t write_calibration(int8_t calibration)
{
	return write(ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION, ZZB_ZCL_INT8,
		     (uint8_t)calibration);
}

static void set_sequence(uint8_t sequence)
{
	zassert_ok(zzb_zcl_attr_set(test_node(), THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT,
				    ZZB_ZCL_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION, &sequence));
}

static void check_setpoints(int16_t cooling, int16_t heating, int16_t unoccupied_cooling,
			    int16_t unoccupied_heating)
{
	zassert_equal(thermostat60.occupied_cooling_setpoint, cooling);
	zassert_equal(thermostat60.occupied_heating_setpoint, heating);
	zassert_equal(thermostat60.unoccupied_cooling_setpoint, unoccupied_cooling);
	zassert_equal(thermostat60.unoccupied_heating_setpoint, unoccupied_heating);
}

static void check_limits(int16_t min_heat, int16_t max_heat, int16_t min_cool, int16_t max_cool)
{
	zassert_equal(thermostat60.min_heat_setpoint_limit, min_heat);
	zassert_equal(thermostat60.max_heat_setpoint_limit, max_heat);
	zassert_equal(thermostat60.min_cool_setpoint_limit, min_cool);
	zassert_equal(thermostat60.max_cool_setpoint_limit, max_cool);
}

/*
 * Section 16: without records the limits are the absolute limits, the cooling setpoints 2600 and
 * the heating setpoints 2000 within them, and SystemMode the factory value
 */
ZTEST(zzb_zcl_thermostat, test_defaults)
{
	check_setpoints(2600, 2000, 2600, 2000);
	check_limits(700, 3000, 1600, 3200);
	zassert_equal(thermostat60.system_mode, ZZB_ZCL_SYSTEM_MODE_AUTO);

	thermostat60.abs_max_heat_setpoint_limit = 1800;
	thermostat60.abs_min_cool_setpoint_limit = 2800;
	thermostat60.system_mode_default = ZZB_ZCL_SYSTEM_MODE_HEAT;
	zassert_ok(reboot());
	check_setpoints(2800, 1800, 2800, 1800);
	check_limits(700, 1800, 2800, 3200);
	zassert_equal(thermostat60.system_mode, ZZB_ZCL_SYSTEM_MODE_HEAT);
}

/*
 * The writable attributes are saved, and the reset restores the defaults. The records are
 * LocalTemperatureCalibration -10, the setpoints 2700, 2100, 2800 and 1800, the limits 800, 2900,
 * 1700 and 3100, SystemMode Heat and ThermostatProgrammingOperationMode 0x01.
 */
ZTEST(zzb_zcl_thermostat, test_settings)
{
	static const uint8_t req[] = {
		0x00, 0x80, 0x02, 0x10, 0x00, 0x28, 0xf6, 0x11, 0x00, 0x29, 0x8c, 0x0a, 0x12, 0x00,
		0x29, 0x34, 0x08, 0x13, 0x00, 0x29, 0xf0, 0x0a, 0x14, 0x00, 0x29, 0x08, 0x07, 0x15,
		0x00, 0x29, 0x20, 0x03, 0x16, 0x00, 0x29, 0x54, 0x0b, 0x17, 0x00, 0x29, 0xa4, 0x06,
		0x18, 0x00, 0x29, 0x1c, 0x0c, 0x1c, 0x00, 0x30, 0x04, 0x25, 0x00, 0x18, 0x01};
	static const uint16_t reset_attrs[] = {0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015,
					       0x0016, 0x0017, 0x0018, 0x001c, 0x0025};

	receive_ep(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, req, sizeof(req));
	check_frame(0, THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, (uint8_t[]){0x18, 0x80, 0x04, 0x00}, 4);

	zassert_ok(reboot());
	zassert_equal(thermostat60.local_temperature_calibration, -10);
	check_setpoints(2700, 2100, 2800, 1800);
	check_limits(800, 2900, 1700, 3100);
	zassert_equal(thermostat60.system_mode, ZZB_ZCL_SYSTEM_MODE_HEAT);
	zassert_equal(thermostat60.thermostat_programming_operation_mode, 0x01);

	reset_fakes();
	reset_node();
	zassert_equal(thermostat60.local_temperature_calibration, 0);
	check_setpoints(2600, 2000, 2600, 2000);
	check_limits(700, 3000, 1600, 3200);
	zassert_equal(thermostat60.system_mode, ZZB_ZCL_SYSTEM_MODE_AUTO);
	zassert_equal(thermostat60.thermostat_programming_operation_mode, 0);
	check_zcl_cb(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, reset_attrs, ARRAY_SIZE(reset_attrs));
}

/* A setpoint outside the limits of its kind gives INVALID_VALUE (ZCL Table 6-13) */
ZTEST(zzb_zcl_thermostat, test_setpoint_limits)
{
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 699),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT, 3001),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, 3201),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 1599),
		      ZZB_ZCL_INVALID_VALUE);
	check_setpoints(2600, 2000, 2600, 2000);

	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 700),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 3200),
		      ZZB_ZCL_SUCCESS);
	check_setpoints(2600, 700, 3200, 2000);

	/* The limits of the user replace the absolute limits */
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, 2500),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, 2499),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, 2500),
		      ZZB_ZCL_SUCCESS);
}

/*
 * Section 16: a limit needs AbsMin <= Min <= Max <= AbsMax with the occupied and unoccupied
 * setpoints of its kind within it, and the setpoints do not move
 */
ZTEST(zzb_zcl_thermostat, test_limits)
{
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, 699),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT, 3001),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, 1599),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, 3201),
		      ZZB_ZCL_INVALID_VALUE);

	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, 2001),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT, 1999),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, 2601),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, 2599),
		      ZZB_ZCL_INVALID_VALUE);
	check_limits(700, 3000, 1600, 3200);

	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT, 1800),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, 1801),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, 1800),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 3000),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, 2999),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, 3000),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT, 2000),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, 2600),
		      ZZB_ZCL_SUCCESS);
	check_limits(1800, 2000, 2600, 3000);
	check_setpoints(2600, 2000, 3000, 1800);
}

/*
 * ZCL 6.3.2.2.2.2: with ControlSequenceOfOperation 4 or 5 the heating setpoint stays below the
 * cooling setpoint of its pair by 10 x MinSetpointDeadBand, and the other setpoint does not move.
 * The other sequences do not need the dead band.
 */
ZTEST(zzb_zcl_thermostat, test_dead_band)
{
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 2351),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 2350),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, 2599),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT, 2700),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 2249),
		      ZZB_ZCL_INVALID_VALUE);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 2250),
		      ZZB_ZCL_SUCCESS);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT, 2001),
		      ZZB_ZCL_INVALID_VALUE);
	check_setpoints(2700, 2350, 2250, 2000);

	set_sequence(ZZB_ZCL_SEQUENCE_COOLING_HEATING_REHEAT);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 2451),
		      ZZB_ZCL_INVALID_VALUE);
	set_sequence(ZZB_ZCL_SEQUENCE_HEATING_ONLY);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 2900),
		      ZZB_ZCL_SUCCESS);
	set_sequence(ZZB_ZCL_SEQUENCE_COOLING_ONLY);
	zassert_equal(write_temperature(ZZB_ZCL_THERMOSTAT_UNOCCUPIED_COOLING_SETPOINT, 1600),
		      ZZB_ZCL_SUCCESS);
	check_setpoints(2700, 2900, 1600, 2000);
}

/*
 * ZCL 6.3.2.2.2.1 (CCB 2477): LocalTemperatureCalibration outside -25 to 25 takes the nearer end
 * with SUCCESS and one zcl_cb, and the record holds the limited value. A value that the
 * application sets is limited too.
 */
ZTEST(zzb_zcl_thermostat, test_calibration)
{
	static const uint16_t calibration[] = {ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION};
	int8_t value = 100;

	zassert_equal(write_calibration(-30), ZZB_ZCL_SUCCESS);
	zassert_equal(thermostat60.local_temperature_calibration, -25);
	check_zcl_cb(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, calibration, 1);
	zassert_ok(reboot());
	zassert_equal(thermostat60.local_temperature_calibration, -25);

	zassert_equal(write_calibration(26), ZZB_ZCL_SUCCESS);
	zassert_equal(thermostat60.local_temperature_calibration, 25);
	check_zcl_cb(THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT, calibration, 1);
	zassert_equal(write_calibration(-25), ZZB_ZCL_SUCCESS);
	zassert_equal(thermostat60.local_temperature_calibration, -25);

	zassert_ok(zzb_zcl_attr_set(test_node(), THERMOSTAT_EP, ZZB_ZCL_THERMOSTAT,
				    ZZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION, &value));
	zassert_ok(reboot());
	zassert_equal(thermostat60.local_temperature_calibration, 25);
}

/*
 * SystemMode 0x02 and above 0x09 are reserved, and ControlSequenceOfOperation excludes the modes of
 * ZCL Table 6-15. The cases are ControlSequenceOfOperation, SystemMode and the status.
 */
ZTEST(zzb_zcl_thermostat, test_system_mode)
{
	static const uint8_t cases[][3] = {
		{0x04, 0x02, 0x87}, {0x04, 0x0a, 0x87}, {0x04, 0x00, 0x00}, {0x04, 0x09, 0x00},
		{0x00, 0x04, 0x87}, {0x00, 0x05, 0x87}, {0x00, 0x03, 0x00}, {0x00, 0x06, 0x00},
		{0x01, 0x04, 0x87}, {0x01, 0x05, 0x87}, {0x02, 0x03, 0x87}, {0x02, 0x06, 0x87},
		{0x02, 0x04, 0x00}, {0x02, 0x05, 0x00}, {0x03, 0x03, 0x87}, {0x03, 0x06, 0x87},
		{0x05, 0x03, 0x00}, {0x05, 0x04, 0x00},
	};

	ARRAY_FOR_EACH(cases, i) {
		set_sequence(cases[i][0]);
		zassert_equal(write(ZZB_ZCL_THERMOSTAT_SYSTEM_MODE, ZZB_ZCL_ENUM8, cases[i][1]),
			      cases[i][2], "case %zu", i);
	}

	zassert_equal(thermostat60.system_mode, ZZB_ZCL_SYSTEM_MODE_HEAT);
}

ZTEST_SUITE(zzb_zcl_thermostat, NULL, setup, before, after, NULL);
