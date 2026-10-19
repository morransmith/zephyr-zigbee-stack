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
zephyr_library_sources_ifdef(CONFIG_ZZB_NWK
  nwk/ed_timeout.c
  nwk/frame.c
  nwk/join.c
  nwk/leave.c
  nwk/neighbor.c
  nwk/nib.c
  nwk/nwk.c
  nwk/rejoin.c
  nwk/security.c
  nwk/status.c
)
if(CONFIG_ZZB_NWK OR CONFIG_ZZB_APS)
  zephyr_library_sources(common/fc.c)
endif()
if(CONFIG_ZZB_NWK OR CONFIG_ZZB_APS OR CONFIG_ZZB_ZCL)
  zephyr_library_sources(storage/settings.c)
endif()
zephyr_library_sources_ifdef(CONFIG_ZZB_APS
  aps/ack_dup.c
  aps/aps.c
  aps/apsde.c
  aps/binding.c
  aps/cmd.c
  aps/frame.c
  aps/keys.c
  aps/security.c
)
zephyr_library_sources_ifdef(CONFIG_ZZB_ZDO
  zdo/client.c
  zdo/desc.c
  zdo/mgmt.c
  zdo/server.c
  zdo/zdo.c
)
zephyr_library_sources_ifdef(CONFIG_ZZB_BDB bdb/bdb.c bdb/init.c bdb/reset.c bdb/steering.c bdb/tclk.c)
zephyr_library_sources_ifdef(CONFIG_ZZB_ZCL
  zcl/attr.c
  zcl/clusters/basic.c
  zcl/clusters/identify.c
  zcl/clusters/on_off.c
  zcl/foundation.c
  zcl/frame.c
  zcl/zcl.c
)
zephyr_library_sources_ifdef(CONFIG_IEEE802154_RAW_MODE radio/port.c radio/raw.c)
zephyr_library_sources_ifdef(CONFIG_NET_L2_CUSTOM_IEEE802154 radio/port.c radio/custom_l2.c)
