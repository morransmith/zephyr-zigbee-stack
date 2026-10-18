# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zzb_build_radio)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_IEEE802154_RAW_MODE app PRIVATE src/raw_mode.c)
