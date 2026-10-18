# zzb architecture

zzb is a Zigbee 3.0 stack for Zephyr: Zigbee PRO R22 (05-3474-22), Base Device Behavior (BDB) 3.0.1, Zigbee Cluster Library (ZCL) rev 8, Device Type Library rev 2. Clause numbers in this document refer to these revisions.

## 1. Scope

Roles, in implementation order: end device, sleepy end device, router, coordinator, trust center, network co-processor (NCP).

Interoperability targets: ZHA with EmberZNet (EZSP) and Z-Stack coordinators, and R23 trust centers. An R23 trust center accepts the Zigbee 3.0 join mechanism (R23.2 key negotiation method 0x00, BDB 3.1 5.6.5), so R23-only procedures are not required.

Excluded: R23 features (TLVs, dynamic link key, Security_Start, NWK commissioning), Sub-GHz, touchlink, Zigbee Direct. APS fragmentation is added when a use case requires it.

## 2. Packaging and layout

The repository is a Zephyr module and a west T2 manifest repository, following zephyrproject-rtos/example-application. `west.yml` pins Zephyr v4.4.2. `zephyr/module.yml` sets `build.cmake: .` and `build.kconfig: Kconfig`.

```
west.yml  zephyr/module.yml  CMakeLists.txt  Kconfig  LICENSE  NOTICE
include/zzb/      zzb.h aps.h zdo.h bdb.h zcl.h zcl/<cluster>.h types.h
subsys/zzb/
  common/   buf.c queue.c timer.c eui64.c
  radio/    port.h raw.c custom_l2.c
  mac/      frame.c scan.c assoc.c poll.c tx.c
            coord/   beacon.c assoc_resp.c indirect.c fpb.c
  nwk/      frame.c nib.c neighbor.c addr_map.c security.c join.c rejoin.c leave.c ed_timeout.c fc.c
            router/  addr_alloc.c route.c link_status.c btt.c mtorr.c src_route.c
            coord/   formation.c concentrator.c nwk_manager.c
  sec/      ccm.c mmo.c hmac.c aux_hdr.c key_cache.c install_code.c
  aps/      frame.c apsde.c ack_dup.c binding.c group.c keys.c cmd.c tunnel.c
  zdo/      server.c client.c desc.c annce.c mgmt.c
  bdb/      init.c steering.c tclk.c reset.c fb.c formation.c
  zcl/      frame.c attr.c foundation.c report.c clusters/
  tc/       trust center
  storage/  settings.c
  shell/    debug shell
  ncp/      host interface
tests/common/                fake_radio.c medium.c scripted_tc.c fixtures/
tests/unit/<layer>/
tests/integration/<scenario>/
tests/glue/{raw,custom_l2}/
samples/  on_off_light  contact_sensor
```

| Directory | Specification |
|---|---|
| mac | IEEE 802.15.4-2011; R22 Annex D |
| nwk | R22 chapter 3 |
| aps, zdo | R22 chapter 2 |
| sec | R22 chapter 4, Annexes A-C |
| bdb | BDB 3.0.1 |
| zcl | ZCL rev 8, Device Type Library |

## 3. Node context and roles

All stack state is in `struct zzb_node`, passed explicitly to every function, as OpenThread does with `otInstance`. One binary can run several nodes, which the integration tests use.

Kconfig selects capabilities: `CONFIG_ZZB_ROUTER_CAPABLE`, `CONFIG_ZZB_COORDINATOR_CAPABLE`, `CONFIG_ZZB_TRUST_CENTER`. The role and the sleepy flag are runtime properties of each node, so one test binary holds an end device, a router and a trust center.

The device definition (endpoints, clusters, attribute storage) is passed to `zzb_node_init()`. The stack does not use global iterable sections. One settings handler `zzb` dispatches `zzb/<instance>/...` to the registered nodes.

The `net_buf` pool and the PSA key cache are shared by all nodes.

## 4. Execution model

- A dedicated `k_work_q` owns all stack state. Stack code needs no locks.
- Public API calls are asynchronous. The caller owns a request struct that embeds a `k_work`; the result is delivered by callback on the stack thread. Callbacks must not block.
- Synchronous wrappers compare `k_current_get()` with the stack thread and run inline on a match, so a callback that calls them does not deadlock.
- Read-only status is copied to a snapshot protected by a spinlock.
- The driver `tx()` blocks until CSMA-CA and the ACK complete, and calls `ieee802154_handle_ack()` before returning. The stack calls `tx()` from its work queue with one frame in flight. MCPS-DATA.request returns at once and MCPS-DATA.confirm is posted later, so TX can move to a separate radio thread with changes limited to `radio/`.
- Each table has one `k_work_delayable`, armed for its earliest deadline. Layer timers are `k_work_delayable`. No periodic timer runs while nothing is pending, so a sleepy device is not woken.
- The RX entry may be called from ISR context. It copies the PSDU into a stack buffer with `K_NO_WAIT`, puts it on a `k_fifo` and submits a work item.

## 5. Layer interfaces

- Frames and procedures pass between layers only through primitive calls named after the spec primitives: MCPS/MLME, NLDE/NLME, APSDE/APSME (e.g. `zzb_nlme_join_req()`).
- Information bases (MAC PIB, NIB, AIB) are structs inside `struct zzb_node`. Only the owning layer writes them; other layers read them through getters, which correspond to the GET primitives.
- NWK security uses the network key set in the NIB (`nwkSecurityMaterialSet`, R22 4.3.3) and does not read APS tables. A received network key reaches NWK as APSME-TRANSPORT-KEY.indication, then ZDO, then NLME-SET.
- BDB is part of the ZDO application object. It calls NLME, APSME, the ZDO client and ZCL Identify.
- The ZDO server reads the neighbor, route and binding tables directly for Mgmt_Lqi_req, Mgmt_Rtg_req and Mgmt_Bind_req.
- Data polling is in NWK (NLME-SYNC and a poll scheduler). Fast poll is a reference-counted hold, taken by the APS ACK wait, ZDO and ZCL transactions, BDB procedures and OTA.
- Calls between stack layers are direct function calls. A `.request` primitive is defined in the layer that serves it; `.confirm` and `.indication` primitives are defined in the layer above and called directly by the layer below.
- Function pointers are used where several implementations exist at runtime: radio drivers (`struct ieee802154_radio_api`), the local trust center (`node->tc`, NULL when not built), application callbacks.
- Unit tests replace adjacent layers by link-time substitution with the FFF bundled with Zephyr (`zephyr/fff.h`): a test links the sources of the layer under test and defines fakes with the names of the neighbor layer's functions.

## 6. Tables

| Layer | Tables |
|---|---|
| MAC | PIB, in-flight TX, indirect queue, mirror of the driver frame-pending table |
| NWK | neighbor table (parent and children as entries with relationship, timeout, keepalive, incoming frame counter), `nwkAddressMap`, route table, route discovery table, broadcast transaction table, route record table, network keys (two), group ID table |
| APS | binding table, group table, device key-pair table (incoming frame counter, verified state), trust center IEEE address, duplicate rejection table |
| ZDO | descriptors generated from the device definition, transaction table |
| ZCL | reporting slots |

Every table is accessed through find/add/remove functions. The coordinator can replace the linear arrays with indexed storage behind the same functions.

## 7. Buffers

- One frame is one linear `net_buf` from a stack pool; the PSDU is at most 127 bytes. The buffer user data is `struct zzb_frame_meta` (addresses, handle, LQI, RSSI, flags). Headroom covers the MAC, NWK, auxiliary security and APS headers; the MIC is appended at the tail.
- RX: the radio port copies the PSDU out of the driver `net_pkt` and releases the `net_pkt` immediately, returning it to the driver RX pool.
- TX: one `net_pkt` is allocated at init and reused for every `tx()`, with the stack buffer as its fragment.
- Ownership: a request returning a negative error leaves the buffer with the caller. A request returning 0 takes the buffer and produces exactly one confirm. An indication passes the buffer to the upper layer.
- APS retransmissions of a secured frame use a higher frame counter than the original (R22 2.2.8.4.4). APS keeps the plaintext and secures a new copy per attempt. MAC retries resend the same MAC frame.
- The MIC is computed with security level 5 in the auxiliary header, then the level field is set to 0 on air. The receiver sets it back to 5 before verification (R22 4.3.1.1 step 8, 4.4.1.1 step 10). A unit test with a captured frame covers this.

## 8. Radio port

The default backend is `CONFIG_IEEE802154_RAW_MODE`. The nrf5 and esp32 drivers support it without patches, and it builds no IP stack; `samples/net/wpan_serial` uses the same mechanism. The backend for `CONFIG_NET_L2_CUSTOM_IEEE802154` exists for a coordinator that also runs IP over Ethernet or Wi-Fi; raw mode excludes the IP stack. Both backends implement `ieee802154_init()` and `ieee802154_handle_ack()` and pass received frames to `zzb_radio_rx(port, buf, meta)`.

| Item | Behavior |
|---|---|
| TX mode | `IEEE802154_TX_MODE_CSMA_CA` when the driver reports `IEEE802154_HW_CSMA`; otherwise `cca()` and `IEEE802154_TX_MODE_DIRECT` with software backoff |
| Errors | `-EBUSY` maps to CHANNEL_ACCESS_FAILURE, `-ENOMSG` to NO_ACK |
| Retries | in software; neither nrf5 nor esp32 reports `IEEE802154_HW_RETRANSMISSION` |
| Frame pending after a poll | parsed from the ACK frame passed to `ieee802154_handle_ack()`; drivers do not set `ack_fpb` on ACK packets |
| Filters | PAN ID, short address and IEEE address are programmed by the stack |

Frame pending on the parent side:

- nrf5: `IEEE802154_CONFIG_AUTO_ACK_FPB` with `IEEE802154_FPB_ADDR_MATCH_ZIGBEE` and the `IEEE802154_CONFIG_ACK_FPB` table. In Zigbee mode the table lists short addresses without pending data; data requests from extended addresses always get the bit set.
- esp32: the driver returns `-ENOTSUP` for both settings and sets frame pending on every ACK to a data request. When a data request arrives with `ack_fpb` set and nothing is queued for the child, the MAC sends a zero-length data frame without ACK request, so the child can sleep (802.15.4-2011 5.1.6.3). The same rule covers table races on nrf5. The upstream fix maps both settings to `esp_ieee802154_set_pending_mode(ESP_IEEE802154_AUTO_PENDING_ZIGBEE)` and `esp_ieee802154_add_pending_addr()`, and adds the custom-L2 branch to the driver; it is part of milestone 15. Software emulation of auto-ACK is not possible within the ACK turnaround time.

EUI-64 source, first match wins: settings override, `net_if` link address (custom L2), `hwinfo_get_device_id()`, platform hook (ESP32 eFuse). The nrf5 driver sets its EUI-64 only in its iface init, which raw mode does not run.

## 9. Security and frame counters

`sec/` contains pure functions on top of PSA Crypto:

- AES-128-ECB (`PSA_WANT_ALG_ECB_NO_PADDING`) for the Matyas-Meyer-Oseas hash (R22 Annex B.6);
- AES-CCM with a 4-byte tag, `PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4)`, for security level 5 (ENC-MIC-32), the only level used on air;
- HMAC-MMO for the key-transport key, key-load key and verify-key hash (R22 4.5.3, Annex B.1.4);
- install code CRC (CRC-16/X-25, BDB 10.1.1) and install code key derivation (BDB 10.1.2).

Keys are stored as arrays: two network keys and N link-key pairs. The trust center is identified by `apsTrustCenterAddress` (IEEE address). Code must not assume that the trust center is the parent or has short address 0x0000.

Outgoing frame counters: one for NWK and one for APS, global to the node. The NWK counter must survive factory reset, leave and joining another network (R22 4.3.4). Both are persisted as a watermark under a settings key outside the subtree erased by factory reset:

- on boot: counter = watermark, then watermark += `CONFIG_ZZB_FRAME_COUNTER_JUMP` (default 1024) is saved before the first secured TX;
- at runtime: a save is scheduled when the counter reaches watermark - JUMP/2; if the counter reaches the watermark before the save completes, the TX fails with `-EBUSY`.

Incoming frame counters are kept in RAM.

## 10. Persistence

Zephyr settings: NVS on flash parts (nRF52840, ESP32-C6/H2), ZMS on RRAM parts. Each record has its own key and a version field, since writes are atomic per key. Tables that grow on a trust center store one key per entry.

Persisted records:

- NWK: PAN ID, extended PAN ID, short address, channel, nwkUpdateId, parent, network keys and key sequence number;
- APS: link keys, binding table, group table, trust center address;
- BDB: `bdbNodeIsOnANetwork`;
- ZCL: non-volatile attributes and reporting configuration;
- frame counter watermarks.

ZHA writes bindings and reporting configuration once, during pairing. Losing them on reboot stops attribute reports without an error on either side.

## 11. ZCL data model

- Attribute metadata is constant and declared with macros: cluster ID, attribute ID, type, access, flags, offset into the storage struct.
- Attribute values live in a struct per cluster instance, declared by the application. The endpoint list is part of the device definition passed to `zzb_node_init()`.
- `BUILD_ASSERT` checks attribute types against storage field sizes.
- Simple_Desc_rsp and Active_EP_rsp are generated from the same tables. Cluster macros add the mandatory attributes, including ClusterRevision (0xFFFD).
- The application changes attributes with `zzb_zcl_attr_set()`. The call is marshaled to the stack thread and runs the reportable-change check and the settings save. Other threads must not write the storage structs.
- An optional per-cluster callback validates remote writes.
- ZCL is a Kconfig option. An NCP build leaves ZCL to the host.
- Cluster tables may be generated from the project-chip/zap ZCL XML (`zcl-builtin/silabs`, Apache-2.0, ZCL rev 8) and checked against zigbee-herdsman `src/zspec/zcl` (MIT).

## 12. Sleepy end device and power management

- The stack does not call PM APIs. It stops the radio between polls and arms only deadline timers; Zephyr tickless idle does the rest.
- nRF52840: System ON idle with the radio stopped. The MAC uses `IEEE802154_CONFIG_RX_ON_WHEN_IDLE`.
- ESP32-C6/H2: `CONFIG_PM` and `CONFIG_IEEE802154_ESP32_SLEEP_ENABLE`. With sleep enabled the driver does not report `IEEE802154_RX_ON_WHEN_IDLE`, so the MAC opens the post-poll receive window itself: `start()`, wait up to macMaxFrameTotalWaitTime when the ACK had frame pending set, `stop()`.
- A zero-length data frame from the parent means no pending data; the device returns to sleep.
- Poll rate (BDB 6.8): at most 3 s between polls while a response is expected, during joining and during finding & binding. A parent holds a buffered frame for 7.5 s.
- An End Device Timeout Request follows every join and rejoin, with a timeout longer than the slow poll interval. The keepalive method comes from the parent information field of the response.
- Parent loss, after N consecutive failed polls or transmissions: secure rejoin on the current channel, secure rejoin on all channels, trust center rejoin. This also recovers from a channel change missed while asleep.

## 13. Extension points for coordinator and trust center

- Join, rejoin and leave are split into joiner-side and parent-side files sharing the frame codecs. Parent-side files are compiled with `zephyr_library_sources_ifdef(CONFIG_ZZB_ROUTER_CAPABLE ...)`. Small branches use `IS_ENABLED()`; functions contain no `#ifdef`.
- The parent has one "device joined" path. It sends APS Update Device to a remote trust center, or calls the local trust center through `node->tc` ops, which are NULL when `CONFIG_ZZB_TRUST_CENTER` is off. Children of the trust center itself never produce an Update Device frame on air. Received Request Key and Verify Key commands use the same dispatch.
- APS Tunnel is part of the router (milestone 16): a remote trust center reaches a joining grandchild through it.
- Public API tiers: (1) raw APSDE and endpoint registration with a raw handler; (2) ZDO client; (3) ZCL and BDB. An NCP build uses tiers 1 and 2 plus configuration get/set.
- Export and import of keys, frame counters and the device table are defined together with persistence, because zigpy relies on network backup and restore.
- NCP framing is independent of the transport (UART, USB CDC, TCP). Protocol candidates: ZBOSS NCP (zigpy-zboss exists), Z-Stack MT/ZNP (zigpy-znp exists), or a new protocol with a new zigpy radio library. The choice is made at milestone 20.

## 14. Reference code

| Source | License | Use |
|---|---|---|
| Telink Zigbee SDK (telink-semi/telink_zigbee_sdk) | Apache-2.0 | ZCL, BDB, OTA, Green Power source. MAC, NWK, APS and crypto are prebuilt libraries. The source depends on the Telink event loop and must be ported |
| Zephyr `subsys/net/l2/ieee802154` | Apache-2.0 | 802.15.4 frame codec, device-side scan and association |
| OpenThread `src/core/mac` | BSD-3-Clause | sub-MAC (CSMA, retries, ACK wait, energy scan), data poll sender and handler, indirect queue |
| Contiki-NG | BSD-3-Clause | CSMA MAC, software CCM* |
| project-chip/zap `zcl-builtin/silabs` | Apache-2.0 | ZCL rev 8 clusters, attributes, commands, device types in XML |
| zigbee-herdsman (Koenkk) | MIT | ZCL and ZDO definitions; AES-MMO and install code CRC as test reference |
| Ziggurat (zigpy/ziggurat) | Apache-2.0 | Rust NWK, APS, joining, indirect transmission; MMO, HMAC-MMO, key derivation |
| zigbee-rs | MIT or Apache-2.0 | Rust NWK, APS, BDB, ZCL for R22, BDB 3.0.1, ZCL rev 8 (early alpha) |

No C implementation of AES-MMO or install code key derivation under a permissive license exists. Both are written from R22 Annex B.6 and BDB 10.1 and tested against herdsman, Ziggurat and the spec vectors.

## 15. Testing

- Unit tests: ztest on `native_sim/native/64`; CI also runs `native_sim` (32-bit) to catch pointer-size errors.
- Crypto and codec tests use R22 Annex C vectors, BDB 10.1 install code examples and frames captured from real networks.
- Multi-node integration tests: N fake radio devices implementing `ieee802154_radio_api`, connected by an in-process medium with configurable loss and links. The MAC calls the radio API in the same way as on hardware. native_sim time is simulated, so timer behavior is deterministic.
- The raw-mode and custom-L2 shims have single-node tests in `tests/glue/`, modeled on Zephyr `tests/net/ieee802154/custom_l2`.
- `tests/common/scripted_tc.c` plays the trust center side of join sequences captured with a sniffer from EZSP and Z-Stack coordinators on a test network with a known network key.
- Reboot and power-loss tests run on NVS over the flash simulator; the node is destroyed and re-initialized.
- A test asserts that no timer is armed while a sleepy node is idle.
- Under consideration for milestone 5: the fake medium logs frames as hex, and text2pcap and tshark decode them and check MICs in CI, as a decoder independent of the stack.
- Hardware interoperability: joining ZHA with EZSP and Z-Stack coordinators from milestone 11; current measurement from milestone 14.

## 16. Milestones

Each milestone is one feature branch with its tests.

1. Skeleton: `west.yml`, module files, Kconfig, CMake, CI with twister on native_sim and build-only nrf52840dk and ESP32-C6/H2 targets (`west blob fetch hal_espressif`).
2. Common: buffer pool and metadata, stack work queue, request marshaling, timers, `zzb_node_init()`, EUI-64 provider.
3. Security primitives: MMO, HMAC-MMO, CCM level 5 with the level-field rule, auxiliary header, install code.
4. MAC frame codec: data, beacon and command frames.
5. Radio port: fake radio and medium, raw and custom-L2 shims, TX modes and error mapping, frame pending from the ACK.
6. MAC device side: active scan, association with response polling, data poll, software retries, filters.
7. NWK frame codec and security, NIB, neighbor table, frame counter persistence.
8. NWK join, secure rejoin, trust center rejoin, leave (with rejoin flag), End Device Timeout Request, network status, address conflict.
9. APS: APSDE, ACK, retries with a new frame counter, duplicate rejection, link-key security, Transport Key, Request Key, Verify Key, Confirm Key, Switch Key, key-pair table.
10. ZDO server (descriptors, IEEE_addr/NWK_addr, Device_annce, Mgmt_Leave, Bind/Unbind, Mgmt_Lqi, Mgmt_Bind, Mgmt_NWK_Update channel change) and Node_Desc_req client. The Node Descriptor server mask reports stack compliance revision 22: the TCLK exchange sends Request Key only after Node_Desc_rsp from the trust center reports revision 21 or later (BDB 10.2.5).
11. BDB end device: initialization and boot rejoin (7.1), steering of a node not on a network (8.2), TCLK exchange (10.2.5), resets (chapter 9), parent-loss rejoin. Interop: join ZHA on EZSP and Z-Stack and stay joined across reboot.
12. ZCL foundation (read, write, discover attributes, default response), attribute macros, Basic, Identify, On/Off servers. Interop: ZHA creates the entity.
13. Attribute reporting with persistence; finding & binding (BDB 8.4, 8.5).
14. Sleepy end device: poll scheduler with holds, radio off between polls, timeout negotiation, Power Configuration and Poll Control clusters.
15. Router, part 1: MAC coordinator functions (beacons, association response, indirect queue, frame-pending table), stochastic address allocation, child aging and End Device Timeout server, permit join, Parent_annce; esp32 driver patch.
16. Router, part 2: route discovery, link status, broadcast transaction table with passive ACK, many-to-one routes, route record, source-route relay, APS Update Device and Tunnel.
17. OTA Upgrade client with MCUboot (`flash_img`, `boot_request_upgrade()`).
18. Coordinator: network formation (energy scan, PAN ID selection), concentrator and source route table, network manager.
19. Trust center (BDB 10.3): key generation with `psa_generate_random()`, join and rejoin policy, network key update and switch, per-entry storage.
20. NCP; Green Power proxy basic.

## 17. Risks

| Risk | Mitigation |
|---|---|
| The stack passes its own tests but fails with real coordinators | ZHA interop from milestone 11; fixtures from sniffer captures; independent decoder in CI |
| esp32 driver: no frame-pending control, no custom-L2 branch, no CCA-only TX mode, RX_ON_WHEN_IDLE withheld with sleep enabled, binary blobs | raw mode, software retries, zero-length frame fallback, upstream patch, blob fetch in CI |
| Frame counter or persistence loss: the device cannot rejoin, or reports stop | watermark key outside the factory-reset subtree; reboot and power-loss tests; one settings key per record |
| Router and coordinator scope | ZigBee PRO/2007 layer PICS (08-0006-05) as a per-milestone checklist; features outside the ZHA path deferred |
| Sleepy timing and current are not observable on native_sim | driver RX_ON_WHEN_IDLE where available; deadline-only timers with an idle test; current measurement on hardware |

New dependencies accepted with this document: PSA Crypto (Zephyr mbedTLS / TF-PSA-Crypto), settings with NVS or ZMS, FFF, `net_buf`. Zephyr shell, MCUboot and pcap tooling are confirmed at the milestones that introduce them.
