# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZZB)
  zephyr_include_directories(include)
  add_subdirectory(subsys/zzb)
endif()
