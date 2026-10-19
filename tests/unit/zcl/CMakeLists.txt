# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zzb_unit_zcl)

target_include_directories(app PRIVATE ${ZEPHYR_ZZB_MODULE_DIR}/subsys/zzb)
target_sources(app PRIVATE
  src/attr.c
  src/basic.c
  src/fakes.c
  src/foundation.c
  src/frame.c
  src/groups.c
  src/identify.c
  src/on_off.c
  src/power_config.c
  src/report.c
  src/scenes.c
  src/zcl.c
)
