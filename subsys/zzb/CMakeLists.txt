# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(zzb)
zephyr_library_include_directories(.)
zephyr_library_sources(
  common/buf.c
  common/queue.c
)
