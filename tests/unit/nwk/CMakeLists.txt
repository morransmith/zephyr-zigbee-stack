# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zzb_unit_nwk)

target_include_directories(app PRIVATE ${ZEPHYR_ZZB_MODULE_DIR}/subsys/zzb)
target_sources(app PRIVATE
  src/data.c
  src/fakes.c
  src/fc.c
  src/frame.c
  src/join.c
  src/neighbor.c
  src/nib.c
  src/security.c
)
