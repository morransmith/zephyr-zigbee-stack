# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(zzb)
zephyr_library_include_directories(.)
zephyr_library_sources(
  common/buf.c
  common/eui64.c
  common/node.c
  common/queue.c
  common/timer.c
  mac/frame.c
  sec/aux_hdr.c
  sec/ccm.c
  sec/hmac.c
  sec/install_code.c
  sec/key_cache.c
  sec/mmo.c
)
zephyr_library_sources_ifdef(CONFIG_DT_HAS_ESPRESSIF_ESP32_IEEE802154_ENABLED common/eui64_esp32.c)
zephyr_library_sources_ifdef(CONFIG_ZZB_MAC mac/assoc.c mac/mac.c mac/poll.c mac/scan.c mac/tx.c)
zephyr_library_sources_ifdef(CONFIG_ZZB_NWK nwk/frame.c)
zephyr_library_sources_ifdef(CONFIG_IEEE802154_RAW_MODE radio/port.c radio/raw.c)
zephyr_library_sources_ifdef(CONFIG_NET_L2_CUSTOM_IEEE802154 radio/port.c radio/custom_l2.c)
