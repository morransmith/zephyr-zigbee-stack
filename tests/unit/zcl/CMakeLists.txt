# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zzb_unit_zcl)

target_include_directories(app PRIVATE ${ZEPHYR_ZZB_MODULE_DIR}/subsys/zzb)
target_sources(app PRIVATE
  src/alarms.c
  src/appliances.c
  src/attr.c
  src/ballast.c
  src/basic.c
  src/binary_input.c
  src/client.c
  src/color.c
  src/device_temp.c
  src/door_lock.c
  src/fakes.c
  src/foundation.c
  src/frame.c
  src/groups.c
  src/hvac.c
  src/ias_ace.c
  src/ias_wd.c
  src/ias_zone.c
  src/identify.c
  src/level.c
  src/metering.c
  src/on_off.c
  src/ota.c
  src/ota_file.c
  src/poll_control.c
  src/power_config.c
  src/power_profile.c
  src/report.c
  src/scenes.c
  src/schedule.c
  src/sensors.c
  src/thermostat.c
  src/time.c
  src/window_covering.c
  src/zcl.c
)

include(${ZEPHYR_ZZB_MODULE_DIR}/tests/common/ota_file.cmake)
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
set(image ${ZEPHYR_ZZB_MODULE_DIR}/tests/common/mcuboot_image.bin)
generate_inc_file_for_target(app ${image} ${gen_dir}/mcuboot_image.bin.inc)
zzb_ota_file(${image} 0x1234 0x0042 0x01020400 ${gen_dir}/mcuboot_image.zigbee.inc
  ${ZEPHYR_ZZB_MODULE_DIR}/tests/common/ota_index.json ${gen_dir}/ota_index.json.inc)
