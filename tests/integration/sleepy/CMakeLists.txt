# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zzb_integration_sleepy)

target_include_directories(app PRIVATE
  ${ZEPHYR_ZZB_MODULE_DIR}/subsys/zzb
  ${ZEPHYR_ZZB_MODULE_DIR}/tests/common
)
target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_ZZB_MODULE_DIR}/tests/common/fake_radio.c
  ${ZEPHYR_ZZB_MODULE_DIR}/tests/common/parent.c
)

include(${ZEPHYR_ZZB_MODULE_DIR}/tests/common/ota_file.cmake)
zzb_ota_file(${ZEPHYR_ZZB_MODULE_DIR}/tests/common/mcuboot_image.bin 0x1234 0x0042 0x01020400
  ${ZEPHYR_BINARY_DIR}/include/generated/mcuboot_image.zigbee.inc)
