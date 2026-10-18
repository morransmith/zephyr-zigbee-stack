# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(zzb)
zephyr_library_include_directories(.)
zephyr_library_sources(
  common/buf.c
  common/eui64.c
  common/node.c
  common/queue.c
  common/timer.c
  sec/aux_hdr.c
  sec/ccm.c
  sec/hmac.c
  sec/install_code.c
  sec/mmo.c
)
zephyr_library_sources_ifdef(CONFIG_DT_HAS_ESPRESSIF_ESP32_IEEE802154_ENABLED common/eui64_esp32.c)
