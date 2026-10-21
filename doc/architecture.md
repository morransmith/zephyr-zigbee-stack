# zzb architecture

zzb is a Zigbee 3.0 stack for Zephyr: Zigbee PRO R22 (05-3474-22), Base Device Behavior (BDB) 3.0.1, Zigbee Cluster Library (ZCL) rev 8, Device Type Library rev 2. Clause numbers in this document refer to these revisions.

## 1. Scope

Roles, in implementation order: end device, sleepy end device, router, coordinator, trust center, network co-processor (NCP).

Interoperability targets: ZHA with EmberZNet (EZSP) and Z-Stack coordinators, and R23 trust centers. An R23 trust center accepts the Zigbee 3.0 join mechanism (R23.2 key negotiation method 0x00, BDB 3.1 5.6.5), so R23-only procedures are not required.

Excluded: R23 features (TLVs, dynamic link key, Security_Start, NWK commissioning, ZDO restricted mode), Sub-GHz, touchlink, Zigbee Direct. APS fragmentation and distributed security are added when a use case requires them; ZHA networks use centralized security.

## 2. Packaging and layout

The repository is a Zephyr module and a west T2 manifest repository, following zephyrproject-rtos/example-application. `west.yml` pins Zephyr v4.4.2. Its name allowlist includes `mcuboot`, since the image manager of Zephyr links the bootutil library of that module with `CONFIG_MCUBOOT_IMG_MANAGER`, also on native_sim (section 16). `zephyr/module.yml` sets `build.cmake: .` and `build.kconfig: Kconfig`.

```
west.yml  zephyr/module.yml  CMakeLists.txt  Kconfig  LICENSE  NOTICE
include/zzb/      zzb.h device.h aps.h zdo.h bdb.h zcl.h zcl/<cluster>.h types.h
subsys/zzb/
  common/   buf.c queue.c timer.c eui64.c eui64_esp32.c node.c fc.c
  radio/    port.c raw.c custom_l2.c
  mac/      frame.c mac.c scan.c assoc.c poll.c tx.c
            coord/   beacon.c assoc_resp.c indirect.c fpb.c
  nwk/      nwk.c frame.c nib.c neighbor.c addr_map.c security.c join.c rejoin.c leave.c
            ed_timeout.c status.c poll.c
            router/  addr_alloc.c route.c link_status.c btt.c mtorr.c src_route.c
            coord/   formation.c concentrator.c nwk_manager.c
  sec/      ccm.c mmo.c hmac.c aux_hdr.c key_cache.c install_code.c
  aps/      aps.c frame.c apsde.c ack_dup.c keys.c security.c cmd.c binding.c group.c tunnel.c
  zdo/      zdo.c server.c desc.c mgmt.c client.c
  bdb/      bdb.c init.c steering.c tclk.c reset.c fb.c formation.c
  zcl/      zcl.c frame.c attr.c foundation.c report.c transition.c clusters/
  tc/       trust center
  storage/  settings.c
  shell/    debug shell
  ncp/      host interface
tests/common/                fake_radio.c parent.c scripted_tc.c fixtures/
tests/unit/<layer>/
tests/integration/<scenario>/
tests/glue/{raw,custom_l2}/
tests/build/radio/
samples/  common/ light/ output/ ballast/ switch/ controller/ sensor/ covering/ hvac/ pump/ ias/
          door_lock/ energy/ dual_relay/ multisensor/ lock_contact/ power_strip/ ota/
ci/       release.sh check-frame-log.sh
doc/      architecture.md Doxyfile
scripts/  zigbee_ota.py
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

All stack state is in `struct zzb_node`, passed explicitly to every function, as OpenThread does with `otInstance`. One binary can run several nodes, which the integration tests use. Nodes come from a static pool of `CONFIG_ZZB_NODE_COUNT` entries; `zzb_node_init()` returns a pointer into it. The public header declares `struct zzb_node` without its definition, so layer state stays in internal headers.

`struct zzb_node_config` names the radio device of the node, e.g. `DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154))`; with `CONFIG_ZZB_MAC`, `zzb_node_init()` returns `-EINVAL` without one. The node embeds its radio port and its MAC state, and the MAC finds the node of a port with `CONTAINER_OF()`.

Kconfig selects capabilities: `CONFIG_ZZB_ROUTER_CAPABLE`, `CONFIG_ZZB_COORDINATOR_CAPABLE`, `CONFIG_ZZB_TRUST_CENTER`. The role and the sleepy flag are runtime properties of each node, so one test binary holds an end device, a router and a trust center.

The device definition (endpoints, cluster instances, attribute storage) is passed to `zzb_node_init()`. The stack does not use global iterable sections.

`struct zzb_node_config` points to a `struct zzb_device`: the manufacturer code, the power source and the endpoints in ascending order of endpoint ID, each with its profile, device ID, device version and cluster instances (section 16). `include/zzb/device.h`, which `zzb.h` includes, names the Home Automation profile ID (`ZZB_PROFILE_HA`) and the device types of the DTL (`enum zzb_device_id`, e.g. `ZZB_DEVICE_ON_OFF_LIGHT`), and `ZZB_DEVICE_VERSION(id)` gives the device version of a type, the highest Rev of its revision history in the DTL. The node keeps the pointer, so the definition must outlive the node. With `CONFIG_ZZB_ZDO` or `CONFIG_ZZB_ZCL`, `zzb_node_init()` returns `-EINVAL` without one. The application keeps every simple descriptor within apscMaxDescriptorSize (64 octets, 28 clusters); the stack does not check it. The node reaches the value structs of its clusters through the definition, so two nodes in one binary need two definitions. `zzb_node_init()` loads the settings records of the node with `settings_load_subtree_direct("zzb/<n>")`, where n is the index of the node in the pool, and passes each key to its layer. The stack registers no settings handler, so a `settings_load()` call of the application does not reach a node before it exists.

`struct zzb_node_config` also holds the BDB event callback of the application (section 13) and the ZCL callbacks (section 16), which may be NULL. The node keeps them next to the device definition.

`sleepy` in `struct zzb_node_config` makes the node a sleepy end device: its receiver is off outside MLME procedures (section 9), it joins with RxOnWhenIdle 0 and polls its parent (section 10). The node keeps the flag, and MAC, NWK, BDB and ZDO read it there; nwkCapabilityInformation in the NIB and in the record `nwk/net` does not replace it. The power source of the device definition is set independently.

The install code of the device (BDB 10.1.1), with its CRC, is `install_code` and `install_code_len` in `struct zzb_node_config`, or NULL. It is 8, 10, 14 or 18 bytes long; another length or a CRC mismatch makes `zzb_node_init()` return `-EINVAL` before a node is taken from the pool. The node keeps the derived key next to its EUI-64 (section 14).

The `net_buf` pool and the PSA key cache are shared by all nodes. The OTA Upgrade client keeps the context of `flash_img` and the parser of a download outside the node, since the firmware has one upload slot (section 16).

## 4. Execution model

- A dedicated `k_work_q`, shared by all nodes and started at boot, owns all stack state. Stack code needs no locks.
- Public API calls are asynchronous. The caller owns a request struct that embeds a `k_work`; the result is delivered by callback on the stack thread. Callbacks must not block.
- Synchronous wrappers compare `k_current_get()` with the stack thread and run inline on a match, so a callback that calls them does not deadlock.
- The BDB functions are synchronous wrappers that start a procedure on the stack thread and return an error code. The procedure ends with an event of the BDB callback (section 13).
- Read-only status is copied to a snapshot protected by a spinlock.
- The driver `tx()` blocks until CSMA-CA and the ACK complete, and calls `ieee802154_handle_ack()` before returning. The stack calls `tx()` from its work queue with one frame in flight. MCPS-DATA.request returns at once and MCPS-DATA.confirm is posted later, so TX can move to a separate radio thread with changes limited to `radio/`.
- Each table has one `k_work_delayable`, armed for its earliest deadline. Layer timers are `k_work_delayable`. No periodic timer runs while nothing is pending, so a sleepy device is woken only by its poll timer, the keepalive and the deadlines of ZCL (section 17).
- The RX entry may be called from ISR context. It copies the PSDU into a stack buffer with `K_NO_WAIT`, puts it on a `k_fifo` and submits a work item.

## 5. Layer interfaces

- Frames and procedures pass between layers only through primitive calls named after the spec primitives: MCPS/MLME, NLDE/NLME, APSDE/APSME (e.g. `zzb_nlme_join_req()`).
- Information bases (MAC PIB, NIB, AIB) are structs inside `struct zzb_node`. Only the owning layer writes them; other layers read them through getters, which correspond to the GET primitives.
- NWK security uses the network key set in the NIB (`nwkSecurityMaterialSet`, R22 4.3.3) and does not read APS tables. A received network key reaches NWK as APSME-TRANSPORT-KEY.indication, then ZDO, then NLME-SET.
- BDB is part of the ZDO application object. It calls NLME, APSME, the ZDO client and ZCL Identify. ZDO calls BDB when a network key or a trust center link key arrives and when Mgmt_Leave_req has been answered (section 13).
- ZDO passes the frames of application endpoints to ZCL with `zzb_zcl_data_ind()`, and the reset of the node calls `zzb_zcl_reset()` (section 16). Tests that turn ZCL off define both.
- Finding & binding (section 13) sets IdentifyTime with `zzb_zcl_identify()` and sends Identify Query and Add Group If Identifying with `zzb_zcl_send_cmd()`. The Identify cluster calls `zzb_bdb_identify_end()` when IdentifyTime becomes 0 and `zzb_bdb_identify_query_rsp()` for a received Identify Query Response. Tests that turn ZCL or BDB off define the functions of the other layer.
- The Groups and Scenes clusters use the group table of APS through `zzb_apsme_add_group()`, `zzb_apsme_remove_group()`, `zzb_apsme_remove_all_groups()`, `zzb_aps_group_member()`, `zzb_aps_group_capacity()` and `zzb_aps_groups_of()` (section 11).
- ZDO reads nwkAddressMap with `zzb_nwk_addr_map_find()` and writes it with `zzb_nlme_set_addr_map()`; APS reads it with `zzb_nwk_addr_map_find()` and `zzb_nwk_addr_map_ext()` (section 10).
- The ZDO server reads the neighbor, route and binding tables directly for Mgmt_Lqi_req, Mgmt_Rtg_req and Mgmt_Bind_req.
- Data polling is in NWK: a poll scheduler sends MLME-POLL (section 10). NLME-SYNC is not implemented, since no layer above NWK requests a poll. A layer that waits for frames holds the fast poll rate with `zzb_nwk_poll_hold(node, hold, on)`, one bit per layer (`ZZB_NWK_HOLD_APS`, `ZZB_NWK_HOLD_ZDO`, `ZZB_NWK_HOLD_BDB`, `ZZB_NWK_HOLD_ZCL`). Each layer sets or clears its bit where it recomputes the timer of its waits, and its init clears the bit, so a table that a reset clears without confirms leaves no hold behind. `zzb_nwk_poll_fast(node)` keeps the fast rate for `CONFIG_ZZB_NWK_FAST_POLL_MS` from now; APS calls it for a delivered data frame and ZCL after its own requests. Poll Control sets the intervals with `zzb_nwk_set_poll_intervals(node, long_ms, short_ms)` and checks the source of its commands with `zzb_aps_bound()` (section 16). The OTA Upgrade client has a bit of its own, `ZZB_NWK_HOLD_OTA`, since Poll Control writes `ZZB_NWK_HOLD_ZCL` whenever it rearms its timer (section 16).
- Calls between stack layers are direct function calls. A `.request` primitive is defined in the layer that serves it; `.confirm` and `.indication` primitives are defined in the layer above and called directly by the layer below.
- Function pointers are used where several implementations exist at runtime: radio drivers (`struct ieee802154_radio_api`), the local trust center (`node->tc`, NULL when not built), the hooks of ZCL cluster definitions, application callbacks.
- Unit tests replace adjacent layers by link-time substitution with the FFF bundled with Zephyr (`zephyr/fff.h`): a test links the sources of the layer under test and defines fakes with the names of the neighbor layer's functions. Zephyr links libzzb with `--whole-archive`, so each layer has a Kconfig option that removes it from the build. `CONFIG_ZZB_MAC` defaults to y when a radio backend is enabled; the glue tests turn it off because they fake `zzb_radio_rx()` and `zzb_radio_tx_done()`. `CONFIG_ZZB_NWK` defaults to the value of `CONFIG_ZZB_MAC`; the MAC tests turn it off, and the NWK tests turn it on with the MAC off. `CONFIG_ZZB_APS` defaults to the value of `CONFIG_ZZB_NWK`; the NWK tests turn it off, and the APS tests turn it on with NWK off. `CONFIG_ZZB_ZDO` defaults to the value of `CONFIG_ZZB_APS`; the APS tests turn it off, and the ZDO tests turn it on with APS off. `CONFIG_ZZB_BDB` defaults to the value of `CONFIG_ZZB_ZDO`; the ZDO tests turn it off, and the BDB tests turn it on with ZDO off. `CONFIG_ZZB_ZCL` defaults to the value of `CONFIG_ZZB_BDB`; the BDB tests turn it off, and the ZCL tests turn it on with BDB off. The suites of APS, ZDO, BDB and ZCL define fakes of the NWK poll functions that their layer calls, and the ZCL suite also of `zzb_aps_bound()` and `zzb_apsme_get_tc_addr()`.

## 6. Tables

| Layer | Tables |
|---|---|
| MAC | PIB, TX queue, indirect queue, mirror of the driver frame-pending table |
| NWK | neighbor table (parent and children as entries with relationship, timeout, keepalive, incoming frame counter), network descriptors of the last discovery, `nwkAddressMap`, route table, route discovery table, broadcast transaction table, route record table, network keys (two), group ID table |
| APS | device key-pair table (key attributes, link key type, incoming frame counter), trust center IEEE address, duplicate rejection table, transmission table, binding table, group table |
| ZDO | descriptors generated from the device definition, transaction table |
| BDB | respondents of finding & binding |
| ZCL | reporting table, scene table, alarm table |

Every table is accessed through find/add/remove functions. The coordinator can replace the linear arrays with indexed storage behind the same functions.

## 7. Buffers

- One frame is one linear `net_buf` from a stack pool; the PSDU is at most 127 bytes. The buffer user data is `struct zzb_frame_meta`: source and destination MAC addresses (addressing mode, PAN ID, short and extended address), MSDU handle, LQI, RSSI and flags (ACK request, indirect transmission, frame pending). Headroom covers the MAC, NWK, auxiliary security and APS headers; the MIC is appended at the tail.
- RX: the radio port copies the PSDU out of the driver `net_pkt` and releases the `net_pkt` immediately, returning it to the driver RX pool.
- TX: each radio port embeds a zeroed `struct net_pkt` that is not taken from the `net_pkt` slab, and passes it to every `tx()` with the stack buffer as the fragment. The drivers read the frame from the fragment and only control block flags from the packet.
- Ownership: a request returning a negative error leaves the buffer with the caller. A request returning 0 takes the buffer and produces exactly one confirm. An indication passes the buffer to the upper layer.
- MCPS-DATA.request returns `-EMSGSIZE` when the headroom is shorter than the MAC header it writes, at most `ZZB_MAC_HDR_MAX_LEN` (23) bytes, or the frame is longer than 125 bytes (aMaxPHYPacketSize without the FCS). MCPS-DATA.confirm carries the MSDU handle, and the MAC releases the buffer.
- NWK frames have short MAC addresses with PAN ID compression, so NWK reserves `ZZB_MAC_DATA_HDR_LEN` (9) bytes for the MAC header. `ZZB_NLDE_HEADROOM` is 9 + 8 (NWK header of a data frame) + 14 (auxiliary header) = 31 bytes. `ZZB_APSDE_HEADROOM` adds 9 (APS header with a group address) and 13 (auxiliary header with the extended nonce); APS checks the headroom the frame actually needs. The tailroom holds two MICs, 8 bytes. An ASDU can reach 82 bytes with NWK security and 65 with APS security, the limits of a 125-byte frame.
- APS retransmissions of a secured frame use a higher frame counter than the original (R22 2.2.8.4.4). APS keeps the plaintext and secures a new copy per attempt. MAC retries resend the same MAC frame.
- The MIC is computed with security level 5 in the auxiliary header, then the level field is set to 0 on air. The receiver sets it back to 5 before verification (R22 4.3.1.1 step 8, 4.4.1.1 step 10). A unit test with a captured frame covers this.

## 8. Radio port

The default backend is `CONFIG_IEEE802154_RAW_MODE`. The nrf5 and esp32 drivers support it without patches, and it builds no IP stack; `samples/net/wpan_serial` uses the same mechanism. The backend for `CONFIG_NET_L2_CUSTOM_IEEE802154` exists for a coordinator that also runs IP over Ethernet or Wi-Fi; raw mode excludes the IP stack. Only the nrf5 driver supports it; the esp32 driver has no custom-L2 branch.

`radio/port.c` is shared by both backends:

- `zzb_radio_init(port, dev)` binds a port to a radio device. It returns `-ENOTSUP` if the driver does not report `IEEE802154_HW_TX_RX_ACK`.
- `zzb_radio_tx(port, buf)` queues one frame and returns `-EBUSY` while the previous frame of the port waits for its confirm. A work item on the stack queue runs CSMA-CA and `tx()`, then calls `zzb_radio_tx_done(port, buf, status, frame_pending)`, which may queue the next frame.
- The RX entry copies the frame into a stack buffer, and a work item passes it to `zzb_radio_rx(port, buf)`. LQI and RSSI are in the buffer metadata.
- `zzb_radio_tx_done()` and `zzb_radio_rx()` are defined by the MAC.
- `zzb_radio_eui64(dev, eui64)` reads the `net_if` link address in the custom-L2 backend and returns `-ENODEV` in raw mode.
- `ieee802154_handle_ack()` is defined once for both backends. Since `tx()` blocks the single stack queue, one frame is in flight for all nodes, and the ACK is recorded in file-scope state.
- The TX work item holds a lock of all PM states (section 17) from before the backoff and CSMA-CA until `tx()` returns, since the drivers wait for the TX done and the ACK inside `tx()`.

In raw mode no `net_if` exists, and the drivers pass a NULL interface to `net_recv_data()` and `ieee802154_handle_ack()`. The raw backend therefore serves one port. It implements `net_recv_data()`, which the driver calls for each received frame; `ieee802154_init()` is an empty macro. The custom-L2 backend implements an empty `ieee802154_init()`, finds the interface of the device with `net_if_lookup_by_dev()` and keeps the port in the L2 data of the interface. It receives frames through the `recv` function of its `NET_L2_INIT()` entry. Its `send` returns `-ENOTSUP`, and it does not start the radio; the MAC does.

| Item | Behavior |
|---|---|
| TX mode | `IEEE802154_TX_MODE_CSMA_CA` when the driver reports `IEEE802154_HW_CSMA`, after a wait of 0 to 7 unit backoff periods with `CONFIG_ZZB_RADIO_TX_BACKOFF`; otherwise unslotted CSMA-CA with `cca()` and `k_busy_wait()` (802.15.4-2011 5.1.1.4; macMinBE 3, macMaxBE 5, macMaxCSMABackoffs 4, unit backoff period 320 us), then `IEEE802154_TX_MODE_DIRECT` |
| Status | `enum zzb_mac_status` with the 802.15.4-2006 values. NO_ACK (0xe9) for `-ENOMSG`, and for a frame with the ACK request bit when `ieee802154_handle_ack()` was not called during `tx()`, also if `tx()` returned 0. CHANNEL_ACCESS_FAILURE (0xe1) for every other error. SUCCESS (0x00) otherwise |
| Retries | in software; neither nrf5 nor esp32 reports `IEEE802154_HW_RETRANSMISSION` |
| Frame pending after a poll | parsed from the ACK frame passed to `ieee802154_handle_ack()`; drivers do not set `ack_fpb` on ACK packets |
| Filters | the MAC programs the IEEE address, PAN ID and short address at init and on every change; neither nrf5 nor esp32 programs the IEEE address in raw mode |
| Channel | `set_channel()` followed by `start()`, since the esp32 driver applies a new channel only when it next enters TX or RX |
| Trailing bytes | `CONFIG_IEEE802154_L2_PKT_INCL_FCS` defaults to y in raw mode. The port then drops the last two bytes of a received frame: nrf5 puts the FCS or the LQI there, esp32 the RSSI and the LQI |

The esp32 driver returns 0 from `tx()` after a CCA failure and after a missing ACK, because it discards the error passed by the HAL. A unicast frame then reports NO_ACK by the rule above, and a broadcast frame lost to a CCA failure reports SUCCESS. Its CSMA-CA is one CCA without backoff, and the HAL refuses to transmit while a frame is received, so an attempt fails within about 11 us. Without a wait, a frame sent while the coordinator broadcast failed its MAC retries and the next two frames within about 1 ms, and the node reported parent link failure and rejoined during OTA downloads (milestone 19). `CONFIG_ZZB_RADIO_TX_BACKOFF`, default y with the esp32 driver, spreads the attempts. The CSMA-CA of the port cannot replace it: the HAL passes the busy flag of the CCA to `esp_ieee802154_cca_done()`, whose parameter means a free channel, so `cca()` of the driver reports a free channel as busy.

Frame pending on the parent side:

- nrf5: `IEEE802154_CONFIG_AUTO_ACK_FPB` with `IEEE802154_FPB_ADDR_MATCH_ZIGBEE` and the `IEEE802154_CONFIG_ACK_FPB` table. In Zigbee mode the table lists short addresses without pending data; data requests from extended addresses always get the bit set.
- esp32: the driver returns `-ENOTSUP` for both settings and sets frame pending on every ACK to a data request. When a data request arrives with `ack_fpb` set and nothing is queued for the child, the MAC sends a zero-length data frame without ACK request, so the child can sleep (802.15.4-2011 5.1.6.3). The same rule covers table races on nrf5. The upstream fix maps both settings to `esp_ieee802154_set_pending_mode(ESP_IEEE802154_AUTO_PENDING_ZIGBEE)` and `esp_ieee802154_add_pending_addr()`, adds the custom-L2 branch to the driver, and returns `-EBUSY` and `-ENOMSG` from `tx()`; it is part of milestone 20. Software emulation of auto-ACK is not possible within the ACK turnaround time.

Receiver of a sleepy node (section 9): the driver must report `IEEE802154_RX_ON_WHEN_IDLE`, otherwise `zzb_node_init()` returns `-ENOTSUP`. The MAC uses `configure(IEEE802154_CONFIG_RX_ON_WHEN_IDLE)` and `stop()`. In v4.4.2 both nrf5 and esp32 report it, and both transmit from sleep.

- nrf5: `configure()` with FALSE puts an idle radio to sleep at once. After a TX the radio sleeps with RxOnWhenIdle FALSE and receives otherwise; after an ACK with frame pending to a data request it receives until a frame arrives or `stop()`.
- esp32: `configure()` only writes the PIB, and `stop()` calls `esp_ieee802154_sleep()`. After a TX and its ACK the radio receives with RxOnWhenIdle TRUE and sleeps otherwise, also right after an ACK with frame pending.

The stack holds the EUI-64 as a `uint64_t` and converts it at the frame codec, the radio filter and the hardware sources. EUI-64 source, first match wins:

1. `eui64` in the node configuration, used by tests that run several nodes in one binary;
2. settings override;
3. `net_if` link address (custom L2), read with `zzb_radio_eui64()`;
4. ESP32 eFuse, `esp_efuse_mac_get_default()`: the MAC-48 with the eFuse MAC_EXT field inserted, the same value the esp32 driver uses;
5. `hwinfo_get_device_id()`: an ID of 8 bytes or more gives its first 8 bytes, a 6-byte ID is a MAC-48 and gets FF:FE inserted after the OUI, a shorter ID is padded with leading zeros.

The nrf5 driver sets its EUI-64 only in its iface init, which raw mode does not run.

## 9. MAC

### Frame codec

`mac/frame.c` is a byte codec in the style of `sec/aux_hdr.c`: its functions take a pointer and a length and do not use `net_buf` or `struct zzb_frame_meta`. The MAC layer copies addresses and flags between the header struct and the frame metadata.

- The header struct holds the frame type, the ACK request and frame pending flags as `ZZB_FRAME_ACK_REQ` and `ZZB_FRAME_PENDING`, the sequence number, and the destination and source addresses as `struct zzb_mac_addr`. On transmit, PAN ID compression is set when both addresses are present and their PAN IDs are equal. On receive, a compressed source PAN ID is copied from the destination.
- Transmitted frames have frame version 0 and Security Enabled 0 (R22 D.5; the PICS excludes MAC security, item MS2). Received frames with version 0 or 1 are parsed. Version 2 frames (Enhanced Beacon and Enhanced Beacon Request, optional at 2.4 GHz, R22 D.9.1) and frames with Security Enabled set are rejected with `-ENOTSUP`. This departs from R22 D.5, which requires reception of every non-reserved frame version.
- The parser accepts every addressing variant in R22 Tables D-1 to D-3.
- Commands: association request and response, data request, orphan notification, beacon request, coordinator realignment. Disassociation notification and PAN ID conflict notification are excluded by the PICS, GTS is not used; the parser returns `-ENOTSUP` for them. Coordinator realignment is sent without the Channel Page field (R22 D.5); a received Channel Page field is ignored.
- Beacons: the codec reads the Superframe Specification field, skips the GTS and pending address fields and returns the offset of the beacon payload. The Zigbee beacon payload (R22 3.6.7) is part of the NWK frame codec (milestone 7).
- The FCS is not written or checked. The nrf5 and esp32 drivers add it on transmit. On receive, the radio port drops the two trailing bytes when `CONFIG_IEEE802154_L2_PKT_INCL_FCS` is set (section 8).

### Device side

- `CONFIG_ZZB_MAC` builds `mac.c`, `tx.c`, `scan.c`, `assoc.c` and `poll.c`; `frame.c` is always built.
- One TX FIFO per node holds data and command frames, and its head is the frame in flight. A frame with the ACK request bit is sent again after NO_ACK, up to macMaxFrameRetries (3) times, with the same sequence number (802.15.4 5.1.6.4.3). CHANNEL_ACCESS_FAILURE is reported without a retry. macDSN starts at a random value.
- MLME-SCAN, MLME-ASSOCIATE and MLME-POLL exclude each other: a second request returns `-EBUSY`. Scan and association also return `-EBUSY` while the TX FIFO holds frames, since those frames would go out on the channel being scanned or on the channel of the coordinator. Each procedure allocates one command buffer and reuses it for its frames. MCPS-DATA.request always enqueues. A data frame enqueued during a scan is sent on the channel being scanned, so NWK must not send data while it scans.
- The source address of a transmitted frame is the short address when macShortAddress is below 0xfffe, otherwise the extended address (802.15.4 5.1.6.1).
- Active scan (802.15.4 5.1.2.1.2): the PAN ID filter is 0xffff during the scan. On each channel the MAC sends a beacon request and listens for aBaseSuperframeDuration * (2^n + 1) symbols; after a failed beacon request it moves to the next channel. Frames other than beacons are dropped. Every beacon produces MLME-BEACON-NOTIFY.indication with the channel and the Superframe Specification field, without duplicate filtering. MLME-SCAN.confirm carries only the status: SUCCESS, or NO_BEACON when no beacon arrived. The PAN ID filter and the channel are restored after the scan.
- Association (802.15.4 5.1.3.1): the association request and its data request use the first header form of R22 Tables D-1 and D-2, with source PAN ID 0xffff and the extended source address. The data request follows macResponseWaitTime after the ACK of the request. After any failure macPANId is 0xffff again.
- Data poll (802.15.4 5.1.6.3): after an ACK with frame pending, the MAC waits for a data frame from the polled coordinator. A zero-length frame or the end of the wait gives NO_DATA. MLME-POLL.confirm precedes MCPS-DATA.indication. The frame pending bit of the received frame reaches NWK in the frame metadata, and NWK decides whether to poll again.
- Outside these procedures, data frames with a payload produce MCPS-DATA.indication. Zero-length data frames, beacons and commands are dropped.
- On a sleepy node (section 3) the receiver is on only during MLME procedures. `zzb_mac_init()` sets RxOnWhenIdle FALSE and calls `stop()`; the start of a scan, an association or a poll sets RxOnWhenIdle TRUE, and its end sets FALSE and calls `stop()`. Each procedure begins with a TX, after which both drivers receive, and the scan calls `start()` on every channel. MLME-SET of the channel outside a procedure calls `start()`, then sets RxOnWhenIdle FALSE and calls `stop()`. A data frame outside a procedure goes from sleep, and the driver sleeps again after its ACK. During an association the receiver stays on for macResponseWaitTime, once per join. A node that is not sleepy keeps its receiver on.
- The MAC holds a lock of all PM states (section 17) while the receiver of the node may be on. A sleepy node takes it where it sets RxOnWhenIdle TRUE and releases it after `stop()`; a flag in `struct zzb_mac` makes a call that does not change the state do nothing, such as RxOnWhenIdle FALSE from MLME-SET of the channel outside a procedure. A node that is not sleepy takes the lock in `zzb_mac_init()` and keeps it. `zzb_mac_init()` first releases the lock of a previous init of the node.
- MLME-SET of macPANId, macShortAddress and phyCurrentChannel (`zzb_mlme_set_pan_id()`, `zzb_mlme_set_short_addr()`, `zzb_mlme_set_channel()`) writes the PIB and the radio. `zzb_mlme_set_channel()` returns `-EBUSY` during an MLME procedure, since the end of a scan restores the channel. MLME-GET of macCoordExtendedAddress (`zzb_mlme_get_coord_ext_addr()`) gives NWK the IEEE address of the parent after an association. MLME-RESET is not implemented (section 10, Leave and reset).

| Time | Definition | Value at 2.4 GHz |
|---|---|---|
| Scan dwell | aBaseSuperframeDuration (960 symbols) * (2^n + 1) | 138.24 ms for n = 3, the value R22 D.7 recommends |
| macResponseWaitTime | 32 * aBaseSuperframeDuration | 491.52 ms |
| macMaxFrameTotalWaitTime | 802.15.4 Table 52 formula with macMinBE 5, macMaxBE 8 and macMaxCSMABackoffs 4 (R22 D.6), since the frame comes from the CSMA-CA of the parent | 9846 symbols, 157.54 ms |

The 2011 text of the macMaxFrameTotalWaitTime formula has 2^macMinBE in its second term. The value above uses 2^macMaxBE, because the backoffs after the first m use BE = macMaxBE.

## 10. NWK

`CONFIG_ZZB_NWK` builds `nwk/`, `common/fc.c` and `storage/settings.c`. It selects `CONFIG_SETTINGS`; the application enables the settings backend (section 15). Milestone 8 implements the end device side of join, rejoin, leave and the End Device Timeout; the parent side is part of the router (milestone 20).

### Frame codec

`nwk/frame.c` is a byte codec in the style of `mac/frame.c`.

- The header struct holds the frame type, the frame control flags (discover route, multicast, security, source route, destination IEEE address, source IEEE address, end device initiator), the destination and source addresses, radius, sequence number, the IEEE addresses, the multicast control field and the source route subframe. Transmitted frames have protocol version 2 (nwkcProtocolVersion). The parser rejects other protocol versions and inter-PAN frames with `-ENOTSUP` and the reserved frame type with `-EINVAL`. The relay list of the source route subframe is copied into an array of nwkMaxSourceRoute (12) entries; a longer list is rejected with `-ENOTSUP`.
- Commands: all 13 commands of R22 3.4, in a struct with a union like `struct zzb_mac_cmd`. Lists (link status entries, route record relays, network report PAN IDs, link power delta entries) are not decoded: the struct holds the entry count and a pointer to the first encoded entry, which points into the frame after parsing. The writer copies the encoded entries from that pointer.
- Beacon payload (R22 3.6.7): 15 octets. A Protocol ID other than 0 gives `-ENOTSUP`, a shorter payload `-EINVAL`.

### NIB and neighbor table

- The NIB holds nwkPANId, nwkNetworkAddress, nwkExtendedPANID, nwkSequenceNumber (random initial value, R22 3.6.2.1), nwkUpdateId, nwkCapabilityInformation, nwkParentInformation, the channel of the network (the channel of the nwkMacInterfaceTable entry), nwkSecurityMaterialSet (two key slots with the key, its sequence number and a valid flag), nwkActiveKeySeqNumber and the slot written last. nwkIeeeAddress is the EUI-64 of the node, nwkSecurityLevel is the constant 5, and the default radius is 2 * nwkMaxDepth = 30. Other attributes are added with the milestones that use them. A node is on a network while nwkExtendedPANID is not 0.
- NLME-SET: `zzb_nlme_set_pan_id()` and `zzb_nlme_set_short_addr()` write the NIB and call MLME-SET; `zzb_nlme_set_ext_pan_id()`; `zzb_nlme_set_nwk_key(node, key, seq)` writes the key into the slot with the same sequence number, otherwise into the slot that is not active, and resets the incoming frame counters of that slot in every neighbor entry. The first key becomes the active key. A key set while the key is pending (see Unsecured frames) ends the pending state, saves the NWK records and starts the End Device Timeout Request.
- `zzb_nlme_set_active_key(node, seq)` is NLME-SET of nwkActiveKeySeqNumber, which ZDO calls for Switch Key: the slot with the key sequence number seq becomes active and the key record is saved; without such a slot it returns `-ENOENT`. The outgoing frame counter is not reset above 0x80000000 (R22 4.3.4), since a counter does not reach that value (R22 4.3.4.1).
- `zzb_nlme_set_channel(node, channel, update_id)` is NLME-SET of the channel of nwkMacInterfaceTable and of nwkUpdateId, which ZDO calls for a channel change (section 12). It returns `-EINVAL` for a channel outside 11 to 26 and `-EBUSY` while an NLME procedure runs. It calls MLME-SET of the channel first and writes the NIB and saves the NWK records only when that succeeds, so an MLME error (`-EBUSY` during a poll) does not leave the NIB on a channel other than the channel of the radio.
- NLME-GET: `zzb_nlme_get_short_addr()`, `zzb_nlme_get_ext_pan_id()` and `zzb_nlme_get_capability()` give ZDO nwkNetworkAddress, nwkExtendedPANID and nwkCapabilityInformation.
- `zzb_nwk_key_pending(node)` tells APS whether the network key is pending, the joined and unauthorized state of R22 4.4.1.2.
- A neighbor table entry holds the extended address, network address, device type, relationship, LQI and two incoming frame counters, one per key slot (R22 4.3.1.2 bounds the counters by the number of keys times the number of neighbors). The discovery fields of R22 Table 3-64 are the parsed beacon payload (`struct zzb_nwk_beacon`), the PAN ID, the channel, permit joining and potential parent. The device type is coordinator when the beacon has the PAN Coordinator bit of the Superframe Specification set, otherwise router. RxOnWhenIdle, timeout and keepalive describe children and come with the router (milestone 20). The table has `CONFIG_ZZB_NEIGHBOR_COUNT` entries (default 16); add returns NULL when it is full.
- `zzb_nwk_neighbor_find_short()` skips entries with relationship none, which come from beacons, so the next hop of NLDE-DATA and the LQI update use only the parent and children.

### Address map

- nwkAddressMap holds IEEE and network address pairs of devices that are not neighbors, `CONFIG_ZZB_NWK_ADDR_MAP_COUNT` entries (default 8). APS resolves the IEEE address of a binding through it (section 11).
- `zzb_nlme_set_addr_map(node, ext_addr, addr)` is NLME-SET of an entry: it updates the entry of ext_addr or adds one and saves its record, and returns `-ENOMEM` when the table is full. `zzb_nwk_addr_map_find(node, ext_addr, &addr)` returns 0 or `-ENOENT`; `zzb_nwk_addr_map_ext(node, addr)` returns the IEEE address of addr or 0.
- ZDO adds entries from IEEE_addr_rsp and updates them from Device_annce (section 12). A leave without rejoin and NLME-RESET clear the table and delete the records.

### Frame security

Two functions secure and unsecure in place a buffer whose data starts at the NWK header (R22 4.3.1).

- Outgoing: the active key (none: `-ENOENT`), the next outgoing frame counter (section 14), an auxiliary header with key identifier 1, the extended nonce and the EUI-64 of the node, inserted after the NWK header by moving the header into the headroom, then CCM and the MIC at the tail.
- Incoming: the auxiliary header must have key identifier 1 and the extended nonce, otherwise `-EINVAL`. A frame counter of 2^32-1 or below the stored counter gives `-EALREADY`. The key slot is found by the key sequence number and the neighbor by the source address of the auxiliary header; a missing slot or neighbor gives `-ENOENT`. A MIC mismatch gives `-EBADMSG`. After verification the counter of the slot is set to the received counter + 1.
- During a rejoin the candidate parent may be known only from its beacon, with extended address 0. A frame whose MAC source is the short address of that candidate is then verified against the candidate entry, which takes the source address of the auxiliary header after the MIC check (R22 3.6.1.9.2). Outside a rejoin, frames from devices without a neighbor entry are dropped; an end device receives a broadcast relayed by another router again from its parent.
- A frame secured with the key of the slot written last, when that slot is not active, makes that key active (R22 4.3.1.2 step 7) and saves the key record. A frame secured with the older key does not switch back.
- R22 4.3.1.2 step 8 (unauthenticated child) is part of the router (milestone 20).

### Data service

- `zzb_nlde_data_req(node, buf, dst_addr, radius, secure)` takes the NSDU after `ZZB_NLDE_HEADROOM` bytes of headroom and the NSDU handle in the frame metadata. It returns `-ENOTCONN` while the node has no network address or no parent, `-EBUSY` while an NLME procedure runs, and `-EINVAL` for a handle from 0xf0, which NWK command frames use. The header has frame type data, discover route 1 for unicast and 0 for broadcast, end device initiator set on an end device whose nwkParentInformation is not 0 (R22 3.3.1.1.9), and no IEEE address fields. Radius 0 selects the default radius.
- The MAC destination is the neighbor whose network address is the NWK destination, otherwise the parent. Unicast frames request a MAC ACK. A broadcast from an end device is a MAC unicast to the parent without ACK request (R22 3.6.5).
- Security and MCPS-DATA.request errors are returned by the request, which then leaves the buffer with the caller. MCPS-DATA.confirm produces `zzb_nlde_data_conf(node, handle, status)` with the MAC status.
- Received data frames addressed to the node, to 0xffff or, on a node that is not sleepy, to 0xfffd produce `zzb_nlde_data_ind(node, buf, dst_addr, src_addr, security_use)` with the NSDU in the buffer and the LQI in the metadata. 0xfffd addresses the devices with macRxOnWhenIdle TRUE (R22 Table 3-69).
- Data frames with the multicast flag (R22 3.6.6), which EmberZNet coordinators use for group frames, are passed up with destination 0xfffd; their destination field holds a group ID, which can equal the address of the node, so the flag is checked first. A sleepy node drops them, as frames to 0xfffd. NWK keeps no nwkGroupIDTable: APS drops the frames of groups without a member endpoint (section 11). An end device does not relay them. Multicast command frames are dropped. Frames with the network address of the node as source are dropped, and an end device drops frames with the end device initiator bit (R22 3.6.2.2). Command frames to the same destinations go to the command handlers below. Every received frame updates the LQI of the neighbor whose network address matches the MAC source address.
- There is no broadcast transaction table until milestone 21. A repeated secured frame from the parent is rejected by the incoming frame counter check.
- `zzb_nlde_data_conf()` and `zzb_nlde_data_ind()` are defined by APS. Tests that turn APS off define them.

### Command frames

- NWK command frames are sent through the same function as NLDE-DATA frames. Their MSDU handle is 0xf0 | command identifier; `zzb_mcps_data_conf()` passes other handles to `zzb_nlde_data_conf()`. Command frames have discover route 0 (R22 3.3.1.1.3), the source IEEE address, and the destination IEEE address when it is known.
- An end device handles the rejoin response, leave, End Device Timeout Response and network status commands and drops the others. The network status reply 0x13 to an unknown unicast command (R22 3.6.13) comes with the router (milestone 20); R21 devices do not send it either.

### Unsecured frames

R22 3.6.2.2:

- After a successful association join or trust center rejoin the network key is pending until NLME-SET of a network key. While it is pending, unsecured data frames reach APS, which accepts only Transport Key among them (section 11). NWK cannot check the APS command identifier, because APS encrypts it.
- An unsecured rejoin response is accepted only by a trust center rejoin waiting for it. All other unsecured frames are dropped.

### NLME procedures

- Network discovery, join, rejoin and leave exclude each other; a second request returns `-EBUSY`. A request that fails before anything is sent returns an error and produces no confirm; otherwise exactly one confirm follows. The procedure state is cleared before the confirm, so ZDO can issue the next request from the callback.
- Confirms carry a `uint8_t` status: a value of `enum zzb_nwk_status` (R22 Table 3-73) or the MAC status that R22 passes through; the two ranges do not overlap. `enum zzb_nwk_status_code` holds the codes of R22 Table 3-51, used by network status commands and NLME-NWK-STATUS.indication.
- The `zzb_nlme_*_conf()` and `zzb_nlme_*_ind()` callbacks are defined by BDB (section 13), part of the ZDO application object (section 5); tests that turn BDB off define them.
- One `k_work_delayable` serves the rejoin response wait, a second one the End Device Timeout Request and the keepalive, a third one the poll scheduler. They are rescheduled with `k_work_reschedule_for_queue()`, since `zzb_timer_arm()` only moves a deadline earlier. `zzb_nwk_init()` cancels them first; leave and reset clear the NWK fields one by one and never assign over a `k_work`.

### Network discovery

- `zzb_nlme_network_discovery_req(node, channels, duration)` issues an active MLME-SCAN. At the start of every scan the entries with relationship none are removed and the remaining entries lose potential parent; every beacon of the scan sets it on the entry of its router, so a rejoin does not try a parent it did not hear. R22 3.6.1.4.1 sets potential parent on every entry when a scan starts.
- Beacons with stack profile 2, protocol version 2 and a short source address are used. Each one updates the descriptor of its network (`CONFIG_ZZB_NWK_NETWORK_COUNT` entries, default 8): extended PAN ID, PAN ID, channel, update ID, and permit joining, router capacity and end device capacity ORed over the routers of the network.
- A beacon with association permit set updates the neighbor entry with the same extended PAN ID and short address, or adds one; a full table keeps the descriptor and drops the entry. Beacons without association permit do not enter the table, so the networks on lower channels cannot fill it before the network to join is scanned.
- `zzb_nlme_network_discovery_conf(node, status, networks, count)` carries the MLME-SCAN status. The descriptors stay valid until the next discovery.

### Parent selection

- A candidate has the requested extended PAN ID, potential parent set and capacity for the device type. An association also needs permit joining, and a rejoin a beacon update ID not older than nwkUpdateId (difference as int8 >= 0). The current parent qualifies for a rejoin without capacity, because a parent with a full table accepts the rejoin of a child it knows. The candidate with the highest LQI is chosen.
- The link cost limit of 3 (R22 3.6.1.4.1, 3.6.1.4.2) is not checked, because the LQI scales of the nrf5 and esp32 drivers differ. Depth is not used: R22 3.6.1.4.1 excludes it for stack profile 2, while 3.6.1.4.2 asks for the minimum depth on a rejoin.
- A failed attempt clears potential parent of the candidate, and the next candidate is tried.

### Join

- `zzb_nlme_join_req(node, ext_pan_id, method, channels, duration, capability, secure)` takes method `ZZB_NWK_JOIN_ASSOC` (0x00) or `ZZB_NWK_JOIN_REJOIN` (0x02). It returns `-EINVAL` for extended PAN ID 0, and `-ENOTSUP` for orphaning, a channel change and the router device type (until milestone 20). Every join first sets nwkParentInformation to 0 (R22 3.2.2.13.3) and stops the keepalive.
- Association (R22 3.6.1.4.1): channels and duration are ignored, since the channel comes from the neighbor entry and BDB 8.2 passes 0; R22 3.2.2.13.3 requires a single channel. The request returns `-EALREADY` on a node on a network (R22 INVALID_REQUEST) and `-ENETUNREACH` without a candidate (R22 NOT_PERMITTED). MLME-ASSOCIATE goes to the channel, PAN ID and short address of the candidate. After a failure the next candidate is tried; without one the confirm carries the last MAC status. On success the candidate becomes the parent with the extended address from MLME-GET of macCoordExtendedAddress, the other entries are removed, the NIB takes the PAN ID and address that the MAC already holds, and the key becomes pending. Nothing is saved until the key arrives.
- Rejoin (R22 3.6.1.4.2): an active scan of channels, or of the channel of the network when channels is 0 (BDB 7.1 passes 0). Only beacons with the requested extended PAN ID are used. The NIB PAN ID and the MAC channel are set to those of the candidate. The source address is nwkNetworkAddress; when it is not valid, a random address from 0x0001 to 0xfff7 is written to the NIB and the MAC, since the rejoin response is addressed to it. The rejoin request has radius 1, the source IEEE address, the destination IEEE address when the candidate's is known, NWK security when secure, and the capability with allocate address 0 (stochastic addressing).
- After the confirm of the rejoin request with SUCCESS, NWK waits macResponseWaitTime (491.52 ms) for the rejoin response; a response that arrives before the confirm is accepted too. The response must come from the candidate, with the EUI-64 of the node as destination IEEE address, the IEEE address of the candidate as source when it is known, status SUCCESS and an address below 0xfff8. While a rejoin runs, all other frames are dropped.
- On a sleepy node the end of macResponseWaitTime sends MLME-POLL to the candidate (R22 3.6.1.4.2), which sends the response indirectly to the short address of the request (R22 3.4.7.1), and the wait starts again. Its second end, or the first when the poll cannot be sent, fails the candidate.
- Without any beacon the confirm is NO_NETWORKS, after the last candidate NOT_PERMITTED, and the PAN ID, channel and address of the node before the rejoin are restored. On success the candidate becomes the parent with the IEEE address from the response header, the other entries are removed, and the node takes the address in the response. A trust center rejoin leaves the key pending. A secure rejoin ends a pending key, which a trust center rejoin whose key did not arrive leaves behind, saves the NWK records and starts the End Device Timeout Request.

### End device timeout and keepalive

- The End Device Timeout Request (R22 3.6.10.2) goes to the parent with the timeout `CONFIG_ZZB_NWK_ED_TIMEOUT` (default 8, 256 minutes, the value of nwkEndDeviceTimeoutDefault), configuration 0, radius 1, both IEEE addresses and NWK security. It is started on the keepalive timer with delay 0; a request that cannot be sent (no buffer, frame counter busy) is retried after 1 s.
- A response is accepted only from the parent, checked by the MAC source and the source IEEE address. SUCCESS sets nwkParentInformation and starts the keepalive with a period of timeout / 3 (R22 3.6.10.3). With bit 0 of nwkParentInformation the keepalive is MLME-POLL to the parent; otherwise, with bit 1, it is an End Device Timeout Request answered within macResponseWaitTime. A missing or unsuccessful answer produces NLME-NWK-STATUS.indication with parent link failure (0x09) and stops the keepalive.
- A keepalive that cannot start (MLME-POLL `-EBUSY` or `-ENOBUFS`, an NLME procedure running) is retried after 1 s. After loading the NWK records with nwkParentInformation not 0 the keepalive starts at once (R22 3.6.10.8).
- On a sleepy node every sent request is followed by a poll of the scheduler after macResponseWaitTime, which fetches the response from the parent; the keepalive request then waits 2 × macResponseWaitTime for it. A timer with delay 0 after the poll confirm is not used: `k_work_reschedule_for_queue()` does not remove a work item that is already queued, and the keepalive would send a second request.
- The timeout of a sleepy node is also `CONFIG_ZZB_NWK_ED_TIMEOUT`; the keepalive keeps it for any long poll interval. R22 3.6.10.2 treats a missing response as a failure, so a parent that does not answer, such as Z-Stack Home 1.2, leaves nwkParentInformation 0, and no keepalive runs.

### Poll scheduler

`nwk/poll.c` polls the parent of a sleepy node (BDB 6.8).

- `zzb_nwk_poll(node, addr)` sends MLME-POLL to addr with the PAN ID of the NIB. The keepalive, the scheduler and the rejoin use it.
- The scheduler runs on a sleepy node with a parent, also while the network key is pending. It starts when the node becomes a child after an association or a rejoin and when the NWK records are loaded, and it stops when its handler finds no parent; leave and NLME-RESET cancel it. During an NLME procedure the handler skips the poll and arms the next one.
- The interval is `CONFIG_ZZB_NWK_SHORT_POLL_MS` (default 500) while a hold bit is set (section 5) or the deadline of `zzb_nwk_poll_fast()` has not passed, otherwise `CONFIG_ZZB_NWK_LONG_POLL_MS` (default 5000); the defaults are those of ShortPollInterval and LongPollInterval of Poll Control. `CONFIG_ZZB_NWK_FAST_POLL_MS` defaults to 3000. BDB 6.8 recommends a fast rate of at least one poll per 3 s, and a parent keeps a frame for 7.5 s, so the long default fetches unsolicited frames in time. The next poll is due one interval after the last one; a shorter interval moves it forward, a longer one takes effect after it.
- A received frame with frame pending polls again at once. MLME-POLL.confirm comes before the indication (section 9), so the MAC is idle.
- MLME-POLL `-EBUSY` comes from a keepalive poll, which fetches the same frames, and the scheduler arms its next poll. The keepalive stays as it is.
- On a node that is not sleepy the hold bits and the fast poll deadline are recorded, and no timer is armed.

### Parent link failure

While the node has a parent and no NLME procedure runs, NO_ACK in MCPS-DATA.confirm and MLME-POLL.confirm counts a failure, and SUCCESS and NO_DATA reset the count. CHANNEL_ACCESS_FAILURE means a busy channel and changes nothing. At `CONFIG_ZZB_NWK_PARENT_FAILURES` (default 3) NWK issues NLME-NWK-STATUS.indication with parent link failure and the address of the parent and resets the count; the keepalive failure also resets it. BDB rejoins (section 13).

### Leave and reset

- `zzb_nlme_leave_req(node, rejoin)` is the end device form of NLME-LEAVE.request: the node removes itself and has no children. It returns `-ENOTCONN` when the node is not on a network or has no parent, and `-ENOENT` without a network key. The leave command has destination 0xfffd, radius 1, the source IEEE address and NWK security. It goes to the parent as MAC unicast with ACK request (R22 3.4.4.1), so NLME-LEAVE.confirm reports delivery; the rule for broadcasts from end devices in Data service does not apply. After its confirm the local leave runs, then `zzb_nlme_leave_conf(node, status)`.
- Local leave without rejoin (R22 3.6.1.10.4) clears the NIB, the neighbor table, the network descriptors, the pending key state, the failure count, the keepalive and the rejoin candidate, sets macPANId and macShortAddress to 0xffff through MLME-SET, and deletes the NWK records. The outgoing frame counter and nwkSequenceNumber are kept. MLME-RESET is not used: aborting an MLME procedure whose command frame is in the radio is unsafe, and MLME-SET resets the attributes the MAC holds. With rejoin nothing changes, including nwkExtendedPANID; R22 3.6.1.10.1 clears it, but the rejoin that follows needs it.
- `zzb_nlme_reset_req(node)` is NLME-RESET.request with WarmStart FALSE: the same clearing and deletion of the records without a frame. It returns `-EBUSY` while an NLME procedure runs. BDB uses it when the network key does not arrive (BDB 8.2 step 10), since a leave needs the key, and in the reset of the node (section 13), which also covers a node that has lost its parent and cannot send a leave.
- A received leave request (request 1) is processed only as a unicast from the parent, checked by the MAC source, while no NLME procedure runs. The node sends the leave command with the rejoin flag of the request, performs the local leave and issues `zzb_nlme_leave_ind(node, 0, rejoin)`.
- A received leave indication (request 0) from the parent with remove children makes the node leave in the same way. Without remove children the parent entry is removed, and `zzb_nlme_leave_ind(node, parent IEEE address, rejoin)` and NLME-NWK-STATUS.indication with parent link failure follow, so that BDB rejoins another parent. The R22 text after 3.6.1.10.3.1 can also be read as removing the end device in both cases (R23 has the same wording); with that reading, removing a router from ZHA would take its end device children off the network. A leave indication from another neighbor removes its entry and issues `zzb_nlme_leave_ind(node, its IEEE address, rejoin)`.

### Network status and address conflicts

- A received network status command produces `zzb_nlme_nwk_status_ind(node, code, addr)` with the address in the payload, or the NWK source address when the payload has none. Commands with parent link failure (0x09) or network address update (0x10) are dropped: R22 Table 3-51 defines both for the local device, and a frame from another device must not make BDB rejoin.
- On a node on a network with no NLME procedure running, a frame to the address of the node with a destination IEEE address other than its EUI-64 (R22 3.6.1.9.2, checked after NWK security) is dropped. The node broadcasts a network status command with address conflict (0x0d) and its address to 0xfffd, with the source IEEE address and NWK security, as MAC unicast to the parent without ACK request, and issues NLME-NWK-STATUS.indication with the same code and address; BDB rejoins. The random jitter of R22 3.6.1.9.3 is not applied, because the frame of an end device goes to its parent, which applies the jitter when it relays the broadcast.
- An unsolicited rejoin response from the parent with a new address sets the address, saves the NWK records and produces NLME-NWK-STATUS.indication with network address update (0x10) and the new address (R22 3.6.1.9.3).

## 11. APS

`CONFIG_ZZB_APS` builds `aps/`, `common/fc.c` and `storage/settings.c` and selects `CONFIG_SETTINGS`. Milestone 9 implements the end device side: data service, acknowledgements, retransmissions, duplicate rejection, link-key security and the key commands a joining device exchanges with the trust center.

- The binding table serves ZDO Bind/Unbind and APSDE-DATA with binding (milestone 10). The group table serves the Groups and Scenes servers (milestone 13). Tunnel, Update Device and Remove Device are part of the router (milestone 21).
- Application link keys are excluded: Request Key asks only for the trust center link key, and a received application link key is dropped.
- The primitives are internal and run on the stack thread, as in NWK. The `zzb_apsde_data_conf()`, `zzb_apsde_data_ind()`, `zzb_apsme_transport_key_ind()` and `zzb_apsme_switch_key_ind()` callbacks are defined by ZDO (section 12). `zzb_apsme_confirm_key_ind()` is defined by BDB (section 13). The public tier 1 API (section 18) is not part of milestone 10.

### Frame codec

`aps/frame.c` is a byte codec in the style of `nwk/frame.c`.

- The header struct holds the frame type, delivery mode, ack format, security, ack request and extended header flags, the destination endpoint, group address, cluster, profile, source endpoint and APS counter. The parser rejects inter-PAN frames and fragmented frames (fragmentation sub-field not 0) with `-ENOTSUP` and nonzero reserved fields with `-EINVAL`.
- Commands: Transport Key with its three key descriptors, Request Key, Switch Key, Verify Key and Confirm Key. Other command identifiers give `-ENOTSUP`.

### Data service

- `zzb_apsde_data_req(node, buf, req)` takes the ASDU with the headroom of section 7, the ASDU handle in the frame metadata, and `struct zzb_apsde_req` with the destination address mode and address, destination endpoint, profile, cluster, source endpoint, TxOptions and radius.
- Destination address mode 0x01 sends a group-addressed frame to 0xfffd. Mode 0x02 sends to a network address; a broadcast address gives delivery mode broadcast. Mode 0x03 resolves the IEEE address to 0x0000 when it is apsTrustCenterAddress, otherwise to a neighbor with a relationship, otherwise through nwkAddressMap (section 10), otherwise the request returns `-ENOENT`. An outgoing frame to a network address finds the IEEE address for its APS security in the same order. Mode 0x00 sends to the bound destinations (Binding table below).
- TxOptions uses the R22 bits: security 0x01, acknowledged transmission 0x04, extended nonce 0x10. Use NWK key (0x02) has no effect, since every frame has NWK security. Fragmentation (0x08) returns `-ENOTSUP`, and an acknowledged broadcast or group frame `-EINVAL`.
- A request that fails before anything is sent returns an error and produces no confirm; otherwise exactly one `zzb_apsde_data_conf(node, handle, status)` follows. The status is SUCCESS, NO_ACK (0xa7) or the status of NLDE-DATA.confirm, which R22 2.2.4.1.1 passes through.
- Each request takes an entry of the transmission table (`CONFIG_ZZB_APS_TX_COUNT`, default 4); a full table returns `-ENOBUFS`. The NSDU handle is the entry index. Frames APS sends on its own, acknowledgements and commands, use handle 0xef, and their NLDE-DATA.confirm is ignored.
- An unacknowledged frame is secured in place and passed to NLDE-DATA, and its NLDE-DATA.confirm produces the APSDE-DATA.confirm.
- An acknowledged frame keeps its plaintext in the entry. Every attempt copies it into a new buffer, secures the copy with the next APS frame counter and passes it to NLDE-DATA (section 7). All attempts have the same APS counter.
- The wait for the acknowledgement, apscAckWaitDuration = 0.05 * 2 * nwkcMaxDepth + 0.1 = 1.6 s, starts at NLDE-DATA.confirm with SUCCESS. An acknowledgement that arrives before the confirm is accepted.
- An attempt fails at an NLDE-DATA.confirm other than SUCCESS, at the end of the wait, or when its copy cannot be sent (no buffer, an NLDE-DATA error). The next attempt follows at once after a failed confirm and after apscAckWaitDuration after a send error. After apscMaxFrameRetries (3) retries the confirm carries the status of the last failed NLDE-DATA.confirm, or NO_ACK when there was none.
- The entry is freed before the confirm, so the upper layer can send from the callback. An entry whose acknowledgement arrives before its NLDE-DATA.confirm produces the confirm at the acknowledgement and is freed at the NLDE-DATA.confirm, so each entry has at most one NSDU in flight.
- One `k_work_delayable` serves the table, rescheduled for the earliest deadline with `k_work_reschedule_for_queue()`. While a deadline is set, the function that reschedules it holds the fast poll rate with `ZZB_NWK_HOLD_APS` (section 5), so a sleepy node fetches the acknowledgement from its parent; `zzb_aps_init()` clears the hold.
- An acknowledgement matches an entry by APS counter, cluster, source endpoint equal to the destination endpoint of the frame, and network source equal to its network destination (R22 2.2.8.4.4). APS sends commands without acknowledgement request, so received command acknowledgements are dropped.
- The APS counter starts at a random value, so the duplicate rejection table of a peer does not drop the first frames after a reboot.

### Reception

`zzb_nlde_data_ind()` processes a frame in this order:

1. The header is parsed. Inter-PAN and fragmented frames are dropped, and so are group-addressed frames with APS security or of a group without a member endpoint (Group table below).
2. A frame without NWK security reaches APS only while the network key is pending (section 10). Among such frames only a Transport Key with APS security is accepted.
3. APS security is removed (Frame security below).
4. An acknowledgement goes to the transmission table and does not enter the duplicate rejection table.
5. A command must pass the checks of Commands below. A frame that fails them is dropped without acknowledgement and duplicate entry.
6. A duplicate is acknowledged again when it requests an acknowledgement, and it is not delivered.
7. The acknowledgement of a frame that requests one and was a NWK unicast to the node is built: ack format 1 for a command; for a data frame the endpoints swapped and the cluster, profile and counter copied. It has APS security when the frame had it, with the key that removed it as data key, also when that key is unverified.
8. The frame enters the duplicate rejection table and is delivered with `zzb_apsde_data_ind(node, buf, ind)` or to the command handler. A delivered data frame calls `zzb_nwk_poll_fast()`, since the sender of a request, such as a coordinator that interviews the node, is likely to send the next one; acknowledgements, commands and duplicates do not.
9. The acknowledgement goes to NLDE-DATA with NWK security. An error, such as `-ENOENT` without a network key, drops it.

The acknowledgement follows the delivery because an R23 trust center that is the parent requests an acknowledgement of the network key Transport Key (R23 4.4.11), and after the delivery ZDO has set the key that secures it. The acknowledgement of a trust center link key Transport Key is secured with the old link key, which decrypted the frame, although BDB replaces that key during the delivery. R22 does not say whether acknowledgements carry APS security; a later milestone compares this with a sniffer capture, since milestone 11 had no sniffer.

- The duplicate rejection table (`CONFIG_ZZB_APS_DUP_COUNT`, default 8) holds the network source, the APS counter and an expiry time. R22 does not define the lifetime; an entry lives apscAckWaitDuration * (apscMaxFrameRetries + 1) = 6.4 s, the time a sender with the same constants retransmits. Expiry is checked against `k_uptime_get()` at lookup, so no timer runs. A full table replaces the entry that expires first.
- `struct zzb_apsde_ind` holds the network destination address and the destination endpoint of the frame, the network source address, the source endpoint, profile, cluster and the security status (NWK key, link key). With `group` set, the destination address is the group address and the destination endpoint is not valid. Endpoint 0xff is passed up, and ZDO dispatches it. The LQI is in the frame metadata. The source stays a network address until nwkAddressMap exists.

### AIB and key-pair table

- apsTrustCenterAddress starts as 0xffffffffffffffff, unknown (BDB 10.2.2). Distributed security is excluded (section 1), so the value does not mean a distributed network.
- The preconfigured keys are the key derived from the install code of the node, when it has one (section 3), and the default global trust center link key "ZigBeeAlliance09". BDB 6.4 requires a retail device to join any network, also one that does not know its install code, so both keys are tried (Frame security below). The mode of BDB 6.4 that joins only networks requiring the install code is not implemented.
- `zzb_apsme_get_tc_addr(node)` is APSME-GET of apsTrustCenterAddress, which BDB reads.
- A key-pair entry holds the device address, the key attributes (provisional, unverified, verified), the link key type (unique, global), the link key and the incoming frame counter. The table has `CONFIG_ZZB_APS_KEY_PAIR_COUNT` entries, default 1, since an end device shares a link key only with the trust center. Incoming frame counters are kept in RAM, as in NWK. The outgoing APS frame counter is global to the node (section 14).
- `zzb_apsme_set_tc_addr(node, addr)` sets apsTrustCenterAddress and, when the table has no entry for the address, adds a provisional one with the preconfigured key that decrypted the last accepted Transport Key of a network key: the install code key as unique (R22 4.6.3.1, Table 4-1), "ZigBeeAlliance09" as global. `zzb_apsme_set_link_key(node, addr, key, attributes, type)` adds or replaces the entry of addr with incoming frame counter 0. Both save their records and return `-ENOMEM` when the table is full.
- BDB 10.2.5 step 9 replaces key A by key B in the trust center entry, unique and unverified. Key A is not kept, although the note after step 13 keeps it valid until B is verified: when the verification fails BDB leaves the network, and the next join uses the preconfigured key.
- `zzb_aps_reset(node)` clears the AIB, the key-pair table and the duplicate rejection table and deletes the APS records. The transmission table, the APS counter and the frame counter stay; transmissions in progress end on their own, since NLDE-DATA fails after a leave. The reset of the node calls it (section 13).

### Binding table

- An entry holds the source endpoint, the cluster, the destination address mode (group or IEEE address), the group address, the IEEE address and the destination endpoint. The source is always the node, so its address is not stored. The table has `CONFIG_ZZB_APS_BINDING_COUNT` entries, default 8.
- `zzb_apsme_bind_req(node, binding)` returns 0, also when the same entry exists, `-EINVAL` for another destination address mode and `-ENOMEM` when the table is full. `zzb_apsme_unbind_req(node, binding)` returns 0 or `-ENOENT`. Both save or delete the record of the entry. `zzb_aps_reset()` clears the table and deletes the records.
- APSDE-DATA with destination address mode 0x00 (R22 2.2.4.1.1) uses the entries with the source endpoint and the cluster of the request. A group entry gives a group-addressed frame to 0xfffd; an IEEE address is resolved as in mode 0x03. Entries whose address does not resolve or is the node itself are skipped; the local indication of R22 2.2.4.1.1 is not implemented. Without a destination left the request returns `-ENOENT` and produces no confirm, by the rule of Data service, where R22 issues a confirm with NO_BOUND_DEVICE or NO_SHORT_ADDRESS.
- `zzb_aps_bound(node, ep, cluster, addr, src_ep)` tells whether a binding of ep and cluster has an IEEE destination that resolves to the network address addr, in the order of mode 0x03, and the destination endpoint src_ep. Group bindings do not count. Poll Control checks its clients with it (section 16).
- TxOptions security returns `-ENOTSUP` in mode 0x00: an end device shares a link key only with the trust center, and group frames do not use one. Acknowledged transmission applies to the IEEE destinations only.
- Every destination needs a free entry of the transmission table and a buffer for its frame, which has its own header; both are taken before the first frame is sent, and the request returns `-ENOBUFS` when one is missing. `CONFIG_ZZB_APS_TX_COUNT` limits the destinations of one request.
- Every destination is a transmission of its own with its own APS counter, since frames with the same counter to a device bound twice would meet its duplicate rejection table (R22 2.2.8.4.2). Acknowledgements and retries follow Data service; an unacknowledged destination makes one attempt. A synchronous error of the first destination is returned and nothing is sent; a later destination whose frame cannot be sent follows the rules of a failed attempt.
- The transmission entries of one request carry the index of its first entry. An entry that ends stays in use until the last one ends; then all are freed and one `zzb_apsde_data_conf()` follows, with SUCCESS when every destination succeeded, otherwise the status of the first failed destination in the order of the binding table. R22 does not define the status for several destinations.

### Group table

- An entry is one membership: a group address and an endpoint. R22 2.2.8.3.1 allows any representation of apsGroupTable. The table has `CONFIG_ZZB_APS_GROUP_COUNT` entries, default 8, the minimum of BDB 6.6.
- `zzb_apsme_add_group(node, group, ep)` (R22 2.2.4.5.1) returns 0, also when the entry exists, or `-ENOMEM` when the table is full. `zzb_apsme_remove_group(node, group, ep)` returns 0 or `-ENOENT`. `zzb_apsme_remove_all_groups(node, ep)` removes the entries of the endpoint. Each saves or deletes the records of its entries. The ZCL Groups server checks the group range and the endpoint.
- `zzb_aps_group_member(node, group, ep)` tells whether the endpoint is a member, `zzb_aps_group_capacity(node)` gives the number of free entries, and `zzb_aps_groups_of(node, ep, groups, max)` copies the groups of the endpoint.
- `zzb_aps_reset()` clears the table and deletes the records.
- A received group-addressed frame (delivery mode group) with APS security is dropped, since a group shares no link key. A frame of a group without a member endpoint is dropped. Other frames pass the duplicate rejection table and are delivered once with `group` set in `struct zzb_apsde_ind`; ZCL passes the frame to each member endpoint (section 16). R22 2.2.8.4.2 issues one indication per member endpoint; one indication needs no copy of the frame.
- Group frames are never acknowledged (R22 2.2.8.4.3).

### Frame security

R22 4.4.1:

- `zzb_aps_secure(node, buf, hdr_len, key, key_id, ext_nonce)` secures a buffer whose data starts at the APS header. Key identifier 0 uses the key as data key, 2 the key-transport key HMAC(key, 0x00), 3 the key-load key HMAC(key, 0x02). The function takes the next APS frame counter, inserts the auxiliary header after the APS header through the headroom and appends the MIC. Commands have the extended nonce; data frames have it when TxOptions requests it.
- Frames are encrypted only with provisional or verified entries (R22 4.4.1.1 step 1); acknowledgements are the exception (Reception).
- The sender of an incoming frame is the source address in the auxiliary header. Without the extended nonce it is apsTrustCenterAddress for network address 0x0000, otherwise the neighbor with the network source address, since nwkAddressMap does not exist yet; other frames without the extended nonce are dropped. Z-Stack Home 1.2 sends Transport Key without the extended nonce from the parent, although R22 4.4.1.1 requires it for commands.
- The frame is decrypted with the entry of the sender, whatever its key attributes, since Confirm Key arrives under the unverified key B. Without an entry it is decrypted while the network key is pending (R22 4.4.1.2 step 2) with the install code key, then with "ZigBeeAlliance09". `zzb_sec_ccm_decrypt()` restores the ciphertext after a MIC mismatch, so the second attempt needs no copy of the frame. `struct zzb_aps_sec` points to the preconfigured key that was used, and the acknowledgement of the frame is secured with it. A failed decryption with an entry does not fall back to a preconfigured key.
- A malformed auxiliary header or key identifier 1 gives `-EINVAL`, no key `-ENOENT`, a MIC mismatch `-EBADMSG`. For unique entries a frame counter of 2^32-1 or below the stored counter gives `-EALREADY` (step 4); after verification the stored counter is the received counter + 1.

### Commands

The end device processes the commands a trust center sends (R22 4.4.1.3, 4.4.1.5, 4.4.2.3; BDB 10.2.2). The key identifier must fit the command: 2 for a network key Transport Key, 3 for a trust center link key Transport Key, 0 for the others. While apsTrustCenterAddress is known, the source of a trust center command, in the auxiliary header and in the source address field, must equal it.

- A Transport Key with a network key is accepted (a) with APS security under the key-transport key of an entry or of the preconfigured key, or (b) without APS security when it has NWK security and a NWK broadcast destination, its source address field is apsTrustCenterAddress and the network key is not pending (R22 4.4.1.5). The destination address field must be the EUI-64 of the node or all zeros, and a source address field of 0xffffffffffffffff (distributed security) is dropped. `zzb_apsme_transport_key_ind(node, src_addr, key)` follows; ZDO sets apsTrustCenterAddress and calls NLME-SET (section 5). Under a router parent the network source of the frame is the parent, so the 0x0000 rule does not apply to Transport Key.
- A Transport Key with a trust center link key is accepted under the key-load key of the trust center entry while the network key is not pending, with the destination address field equal to the EUI-64 of the node. BDB decides whether to install the key (BDB 10.2.6).
- Switch Key is accepted while the network key is not pending, with APS security under the trust center entry, or without it from network address 0x0000 with NWK security (R22 4.4.1.3). `zzb_apsme_switch_key_ind(node, src_addr, seq)` follows, and ZDO calls `zzb_nlme_set_active_key()`.
- BDB 10.2.3 and R22 4.9.3 tell a node with an updated trust center link key to ignore trust center commands not secured with it. R22 Table 4-7 forbids APS security on Switch Key, and R22 4.4.1.5 and 4.6.3.4.1 broadcast network key updates with NWK security only, so (b) and Switch Key without APS security are accepted.
- Confirm Key is accepted under the trust center entry, as NWK unicast, with the destination address field equal to the EUI-64 of the node. With SUCCESS and the trust center link key type the entry becomes verified with incoming frame counter 0, and its record is saved. `zzb_apsme_confirm_key_ind(node, status, src_addr, key_type)` follows for every accepted Confirm Key. A Confirm Key with a failure status has no APS security (R22 4.4.8.1.3 step 3a) and is dropped; BDB then fails by timeout.
- Request Key, Verify Key, Update Device, Remove Device, Tunnel and unknown commands are dropped.
- `zzb_apsme_request_key_req(node, dst_addr)` requests the trust center link key (R22 4.4.5.1). It returns `-EINVAL` when dst_addr is not apsTrustCenterAddress and `-ENOENT` without a provisional or verified entry for it. The command is secured with the entry as data key and goes to 0x0000.
- `zzb_apsme_verify_key_req(node, dst_addr, key_type)` follows R22 4.4.7.1.3: `-EINVAL` for a key type other than the trust center link key or a destination other than apsTrustCenterAddress, `-ENOENT` without an entry. The command carries the EUI-64 of the node and HMAC(key, 0x03), has no APS security and goes to 0x0000.
- Commands are sent without acknowledgement request (R22 4.4.10); R23 4.4.11 requests one, and R22 is normative here. Received commands that request an acknowledgement are acknowledged.

## 12. ZDO

`CONFIG_ZZB_ZDO` builds `zdo/`. Milestone 10 implements the ZDO server of an end device, the ZDO client for Node_Desc_req and Device_annce, and the processing of the APS key indications. BDB (section 13), part of the ZDO application object (section 5), defines the NLME callbacks, `zzb_apsme_confirm_key_ind()` and the confirms of the ZDO client.

### Key indications

- A Transport Key with a network key: `zzb_apsme_set_tc_addr(src)`, then on success `zzb_nlme_set_nwk_key(key, seq)` and `zzb_bdb_nwk_key_ind()`. When the first call fails (`-ENOMEM`), the key stays unset and BDB ends the join by its timeout.
- A Transport Key with a trust center link key goes to `zzb_bdb_tc_link_key_ind(node, src, key)`; BDB decides whether to install it (BDB 10.2.5 step 9, 10.2.6).
- Switch Key: `zzb_nlme_set_active_key(seq)`.

### Frames

- ZDO frames use endpoint 0 and profile 0x0000, and their ASDU starts with the transaction sequence number (TSN). A response copies the TSN of the request, has the request cluster with bit 15 set and goes to the network source of the request (destination address mode 0x02).
- The TSN is one counter per node with a random initial value; it advances with every frame ZDO originates (requests, Device_annce).
- Every unicast ZDO frame requests an APS acknowledgement (R23 2.4.2.8.4). R22 requires one only on the replies to broadcast discovery requests (R22 2.4.2.1); the R23 rule includes that case and treats all frames alike. Broadcast frames go without. ZDO frames have NWK security only; requests secured with a link key are accepted too.
- ASDU handles: 0xfe for Mgmt_Leave_rsp, 0xff for the other ZDO frames; values below 0xfe belong to the application, and ZCL responses use 0xfd (section 16). APSDE-DATA.confirm with handles other than 0xfe is ignored.
- A frame that cannot be sent (no buffer, an APSDE-DATA error) is dropped; the requester repeats its request.
- `ZZB_APS_MAX_ASDU` (82) is the longest ASDU of a unicast frame with NWK security only (section 7). The node descriptor and the record count of the management responses use it.

### Reception

- `zzb_apsde_data_ind()` passes frames to endpoint 0 with profile 0x0000 and a non-empty ASDU to ZDO. Group frames and frames with another destination endpoint go to ZCL (section 16); the group flag is checked first, since a group frame has destination endpoint 0 in the indication.
- A request is a broadcast when its network destination is a broadcast address. No error status answers a broadcast request (R22 2.4.4.1).
- Broadcasts of the requests R22 allows only as unicast are dropped: Node_Desc, Power_Desc, Simple_Desc, Active_EP (R22 2.4.2.1), Bind, Unbind, Mgmt_Lqi, Mgmt_Bind and Mgmt_Leave.
- A unicast request without a handler gets a response of the TSN and NOT_SUPPORTED (0x84); such a broadcast is dropped (R22 2.4.4.1). A supported request with a short payload is dropped.
- Responses (cluster bit 15 set) go to the transaction table of the client; responses that match no transaction are dropped.
- A received Device_annce updates the nwkAddressMap entry of its IEEE address, when there is one (R22 2.5.4.5.5); it adds no entry. Address conflict detection from Device_annce is optional for an end device (PICS NLF31, NLF32) and is not implemented. A binding to an IEEE address that is not the trust center, a neighbor or in nwkAddressMap is accepted but skipped when sending (section 11).
- Parent_annce is dropped (R22 2.4.3.1.12).

### Server

Addresses (R22 2.4.3.1.1, 2.4.3.1.2):

- NWK_addr_req for the EUI-64 of the node and IEEE_addr_req for its network address get SUCCESS with both addresses. Request type 0x01 (extended) adds NumAssocDev 0 without the start index and the list.
- Without a match a unicast request gets DEVICE_NOT_FOUND, with the requested IEEE address and 0xffff in NWK_addr_rsp, and with 0xffffffffffffffff and the requested network address in IEEE_addr_rsp. A broadcast request without a match gets no response: R22 2.4.3.1.2 asks for an IEEE_addr_rsp also then, and R22 2.4.4.1, which forbids error responses to broadcasts, is applied.
- A unicast request with a request type above 0x01 gets INV_REQUESTTYPE with the addresses of the node.

Descriptors (R22 2.3.2, 2.4.4.2):

| Node descriptor field | Value |
|---|---|
| Logical type | 2, end device |
| Complex and user descriptor available, APS flags | 0 |
| Frequency band | 2.4 GHz (octet 1 is 0x40) |
| MAC capability flags | nwkCapabilityInformation |
| Manufacturer code | from the device definition |
| Maximum buffer size, maximum incoming and outgoing transfer size | `ZZB_APS_MAX_ASDU`, without fragmentation |
| Server mask | 0x2c00: stack compliance revision 22 in bits 9 to 15, no server role |
| Descriptor capability field | 0 |

- The power descriptor reports current power mode 0 (receiver synchronized with RxOnWhenIdle), on a sleepy node 1 (receiver comes on periodically, R22 Table 2-35), the power source of the device definition as available and current source, and level 100 % (0xc). The battery level comes with the Power Configuration cluster (milestone 14).
- Node_Desc_req, Power_Desc_req and Active_EP_req for another network address get INV_REQUESTTYPE without a descriptor; Active_EP_rsp then has count 0. Active_EP_rsp lists the endpoints in the order of the device definition.
- A simple descriptor lists the server instances of the endpoint as input clusters and the client instances as output clusters, in the order of the endpoint.
- Simple_Desc_req: endpoint 0 or 0xff gives INVALID_EP, then another network address INV_REQUESTTYPE, then an endpoint not in the device definition NOT_ACTIVE, all with length 0 (R22 2.4.4.2.5). A descriptor longer than apscMaxDescriptorSize gets no response.
- Match_Desc_req (R22 2.4.4.2.7) for the network address of the node or a broadcast address matches an endpoint whose profile equals the requested one, or any profile when the request has 0xffff, and which has an input cluster of the request input list or an output cluster of the request output list. A request for another address gets INV_REQUESTTYPE as unicast, with the network address and match length 0 as in Figure 2-70 (the text of 2.4.4.2.7 omits the address), and is dropped as broadcast. Without a match a broadcast gets no response and a unicast gets SUCCESS with match length 0. A SUCCESS response carries the network address of the node, also for a broadcast address in the request.

Bindings (R22 2.4.3.2.2, 2.4.3.2.3):

- A source address other than the EUI-64 of the node gives NOT_SUPPORTED, a source endpoint outside 1 to 254 INVALID_EP, a destination address mode other than 0x01 and 0x03 NOT_SUPPORTED (R22 does not define this case). Then APSME-BIND or APSME-UNBIND runs (section 11): a full table gives TABLE_FULL, a missing entry NO_ENTRY.
- No authorization rule applies: R22 has none, and R23 restricted mode is excluded (section 1).

Management (R22 2.4.3.3, 2.4.4.4):

- Mgmt_Lqi_rsp lists the neighbor entries with a relationship. A record holds nwkExtendedPANID, the extended address (0xffffffffffffffff when unknown), the network address, the device type, RxOnWhenIdle (1 for a coordinator or router, 2 unknown for an end device), the relationship, permit joining 2 (unknown), the depth (0 for the coordinator, from the beacon for a router) and the LQI. The NWK record does not store the depth, so after a reboot a router parent is reported with depth 0; the router (milestone 20) needs the depth of its parent for its own beacon and stores it.
- Mgmt_Bind_rsp lists the binding entries with the EUI-64 of the node as source address; an empty table gives SUCCESS with zero counts and no list.
- Both responses hold as many whole records from the start index as fit in `ZZB_APS_MAX_ASDU`: 3 neighbor records, or 3 bindings with an IEEE destination. A start index past the end gives SUCCESS with list count 0. R22 Table 2-129 limits the neighbor records to two, a value derived from the 802.15.4-2003 frame size.
- Mgmt_Permit_Joining_req, mandatory (R22 Table 2-126), gets INVALID_REQUEST (0xc2) as unicast, the NLME-PERMIT-JOINING.confirm status of an end device (R22 3.2.2.7.3).
- Mgmt_Leave_req (R22 2.4.3.3.5, 3.6.1.10.3.1) for the EUI-64 of the node or 0 gets Mgmt_Leave_rsp SUCCESS with handle 0xfe. Its APSDE-DATA.confirm with any status, or the failure to send it, calls `zzb_bdb_leave(node, rejoin)`, and BDB leaves (section 13). Remove children is ignored. The rejoin flag comes from the request, as in R23; R22 sets it to FALSE. A coordinator that sets rejoin expects the device back, and removal from ZHA sends rejoin 0, so both readings behave alike there. ZDO keeps the flag until the confirm. Another device address gets UNKNOWN_DEVICE (0xc8), the NLME-LEAVE.confirm status for a device that is not a child (R22 3.2.2.18.3).
- Mgmt_NWK_Update_req (R22 2.4.3.3.9, Annex E) with scan duration 0xfe changes the channel. The channel mask must have exactly one bit of channels 11 to 26 on page 0; otherwise a unicast gets Mgmt_NWK_Update_notify INVALID_REQUEST (0xc2). A valid request gets no response. After nwkNetworkBroadcastDeliveryTime (`ZZB_NWK_BROADCAST_DELIVERY_MS`, 9 s at 2.4 GHz) ZDO calls `zzb_nlme_set_channel()` with the nwkUpdateId of the request; an error is ignored, since BDB finds the network by a rejoin on all channels (section 17). A new request during the wait replaces the pending one. R22 Annex E increments nwkUpdateId; ZDO copies it from the request, as R23 Annex E does, because the beacons of the network carry the value of the network manager and parent selection for a rejoin compares it with nwkUpdateId (section 10). The copy equals that value also after a missed update.
- Scan duration 0xff is dropped: the stack keeps neither apsChannelMaskList nor nwkManagerAddr, and an end device that missed a channel change finds the network by a rejoin on all channels (section 17).
- Scan durations 0x00 to 0x05 request an energy scan, which the MAC does not have: a unicast gets Mgmt_NWK_Update_notify NOT_SUPPORTED with the status only, a broadcast is dropped (PICS AZD801: optional for end devices). Other scan durations get INVALID_REQUEST as unicast. The transmission and failure counters of Annex E are not kept.

### Client

- The transaction table has `CONFIG_ZZB_ZDO_TRANSACTION_COUNT` entries (default 4) with the TSN, the request cluster, the destination and the deadline. One `k_work_delayable` serves the table, rescheduled for the earliest deadline with `k_work_reschedule_for_queue()`. While a transaction is in use, the function that reschedules it holds the fast poll rate with `ZZB_NWK_HOLD_ZDO`; `zzb_zdo_init()` clears the hold.
- `zzb_zdo_node_desc_req(node, addr)` sends Node_Desc_req for addr to addr with an APS acknowledgement request. It returns `-ENOBUFS` when the table is full or no buffer is free, and the APSDE-DATA errors; otherwise exactly one `zzb_zdo_node_desc_conf(node, status, addr, server_mask)` follows. BDB reads the stack compliance revision from the server mask (BDB 10.2.5 step 5); other fields are added when a user needs them.
- A response matches by TSN, by the request cluster with bit 15 set and by a network source equal to the destination of the request. The confirm carries the status of the response, with the server mask valid on SUCCESS, or TIMEOUT (0x85) `ZZB_ZDO_RSP_TIMEOUT_MS` (5000 ms) after the request was sent. 5 s is bdbcTCLinkKeyExchangeTimeout, so BDB 10.2.5 steps 3 and 4 use the confirm as their timer. A SUCCESS response with a short descriptor is dropped, and the transaction waits for the timeout.
- `zzb_zdo_simple_desc_req(node, addr, ep)` sends Simple_Desc_req (R22 2.4.3.1.5) and `zzb_zdo_ieee_addr_req(node, addr)` IEEE_addr_req with request type 0x00 (R22 2.4.3.1.2), both to addr, for finding & binding (section 13). Their confirms are `zzb_zdo_simple_desc_conf(node, status, addr, desc)`, where `struct zzb_zdo_simple_desc` points to the cluster lists in the frame, valid during the call, and `zzb_zdo_ieee_addr_conf(node, status, addr, ext_addr)`. A successful IEEE_addr_rsp sets the nwkAddressMap entry before the confirm. Each response is checked for the length its cluster needs; a shorter SUCCESS response is dropped.
- The transaction holds the request cluster, and the response or the timeout ends it with the confirm of that cluster. The entry is freed before the confirm. APSDE-DATA.confirm of a client request is not used: the response or the timeout ends the transaction. Three APS attempts fit into 5 s; the response to the fourth usually comes too late, and the caller repeats the request (BDB 10.2.5 step 4).
- `zzb_zdo_device_annce_req(node)` broadcasts Device_annce to 0xfffd without acknowledgement, with nwkNetworkAddress, the EUI-64 and nwkCapabilityInformation (R22 2.4.3.1.11), and returns an error code. BDB calls it after a join or rejoin (BDB 7.1 step 5, 8.2 step 11) and after an address change (R22 3.6.1.9.2).
- `zzb_zdo_mgmt_permit_joining_req(node, duration, tc_significance)` broadcasts Mgmt_Permit_Joining_req (R22 2.4.3.3.7) to 0xfffc without a transaction and returns an error code. BDB sends it after steering (BDB 8.1, 8.2 step 14).

## 13. BDB

`CONFIG_ZZB_BDB` builds `bdb/`. Milestone 11 implements BDB 3.0.1 for an end device: initialization (BDB 7.1), network steering (BDB 8.1, 8.2), the trust center link key exchange (BDB 10.2.5), the resets of BDB chapter 9 and the rejoin after the loss of the parent. Milestone 13 adds finding & binding (BDB 8.4, 8.5). BDB defines the NLME callbacks, `zzb_apsme_confirm_key_ind()` and the confirms of the ZDO client.

| File | Contents |
|---|---|
| `bdb.c` | init, public functions, events, dispatch of the NLME callbacks, timer, capability of a join, state and fast poll hold |
| `init.c` | initialization and the rejoin series |
| `steering.c` | network steering |
| `tclk.c` | trust center link key exchange |
| `reset.c` | resets and leaves |
| `fb.c` | finding & binding |

### Interface

`include/zzb/bdb.h`:

```c
enum zzb_bdb_status {
	ZZB_BDB_SUCCESS,
	ZZB_BDB_NO_NETWORK,
	ZZB_BDB_TCLK_EX_FAILURE,
	ZZB_BDB_NOT_ON_A_NETWORK,
	ZZB_BDB_NO_IDENTIFY_QUERY_RESPONSE,
	ZZB_BDB_BINDING_TABLE_FULL,
};

enum zzb_bdb_event {
	ZZB_BDB_EVENT_INIT,
	ZZB_BDB_EVENT_STEERING,
	ZZB_BDB_EVENT_REJOIN,
	ZZB_BDB_EVENT_LEFT,
	ZZB_BDB_EVENT_FINDING_BINDING_TARGET,
	ZZB_BDB_EVENT_FINDING_BINDING_INITIATOR,
};

typedef void (*zzb_bdb_cb_t)(struct zzb_node *node, enum zzb_bdb_event event,
			     enum zzb_bdb_status status);

int zzb_bdb_start(struct zzb_node *node);
int zzb_bdb_steering(struct zzb_node *node);
int zzb_bdb_reset(struct zzb_node *node);
int zzb_bdb_finding_binding_target(struct zzb_node *node, uint8_t ep);
int zzb_bdb_finding_binding_initiator(struct zzb_node *node, uint8_t ep, uint16_t group);
```

- The functions run on the stack thread through `zzb_call_sync()`. `zzb_bdb_start()`, `zzb_bdb_steering()` and `zzb_bdb_reset()` return `-EBUSY` while a BDB procedure runs. The pause between rejoin series is a procedure too, except for `zzb_bdb_reset()`, which cancels it. Finding & binding runs beside the procedures (Finding & binding below).
- Every procedure ends with one call of `bdb_cb` (section 3) on the stack thread, which may come before the function returns. BDB clears the state of the procedure before the call, so the callback can start the next procedure.
- Events:
  - INIT: the procedure of `zzb_bdb_start()`, with NOT_ON_A_NETWORK or the result of the rejoin series;
  - STEERING: the procedure of `zzb_bdb_steering()`, with SUCCESS, NO_NETWORK or TCLK_EX_FAILURE;
  - REJOIN: the rejoin series after a parent link failure, an address conflict or a leave with rejoin;
  - LEFT: the node has left its network and is reset. A steering that a reset or a leave cancels ends with LEFT.
  - FINDING_BINDING_TARGET: finding & binding for a target endpoint, with SUCCESS or NOT_ON_A_NETWORK;
  - FINDING_BINDING_INITIATOR: finding & binding for an initiator endpoint, with SUCCESS, NOT_ON_A_NETWORK, NO_IDENTIFY_QUERY_RESPONSE or BINDING_TABLE_FULL.
- A rejoin series reports NO_NETWORK after its first failure and SUCCESS when a later series succeeds, so INIT and REJOIN can come twice.
- bdbCommissioningStatus is the status of the event. IN_PROGRESS is not used, since `-EBUSY` reports a running procedure.

### Attributes

| Attribute or constant | Value |
|---|---|
| bdbPrimaryChannelSet | `CONFIG_ZZB_BDB_PRIMARY_CHANNELS`, default 0x02108800 (channels 11, 15, 20, 25) |
| bdbSecondaryChannelSet | `CONFIG_ZZB_BDB_SECONDARY_CHANNELS`, default 0x05ef7000, the other channels of 0x07fff800 |
| bdbScanDuration | 3 |
| bdbTCLinkKeyExchangeMethod | 0, APS Request Key |
| bdbTCLinkKeyExchangeAttemptsMax | 3 |
| bdbcTCLinkKeyExchangeTimeout | 5 s |
| bdbcMinCommissioningTime | 180 s |
| apsSecurityTimeOutPeriod | 1.7 s, the Zigbee PRO value of PICS 08-0006-05 Table 4 |

- On the CC2531 the network key arrived 130 ms after the association and 616 ms after a trust center rejoin.
- bdbNodeJoinLinkKeyType is not kept. The type of the trust center entry shows it until the key exchange, which makes every entry unique (BDB 10.2.5 step 9).
- bdbNodeIsOnANetwork has no record. `zzb_bdb_init()`, called by `zzb_node_init()` after the settings are loaded, sets it when nwkExtendedPANID is not 0. Step 11 of BDB 8.2 sets it, and the reset clears it. NWK writes `nwk/net` when the network key arrives and deletes it at a leave and at NLME-RESET (section 10), so the record and the attribute change together. A node that loses power during the trust center link key exchange is on the network with key A after the reboot.
- The capability of an association has allocate address, receiver on when idle on a node that is not sleepy (section 3), and mains power (bit 2) when the power source of the device definition includes `ZZB_POWER_MAINS`. ZHA reads these bits from Device_annce and the node descriptor. A rejoin uses the same capability, so a changed sleepy flag applies at the next rejoin.

### Common rules

- One `k_work_delayable` per node serves the key wait, the timers of the key exchange, the pause between rejoin series and the retries below. A second one serves finding & binding for an initiator. `zzb_bdb_init()` cancels both first.
- An NLME request that returns `-EBUSY` is repeated after 1 s. The MAC refuses a scan while its TX FIFO holds frames: at a parent link failure the next frame is queued before MCPS-DATA.confirm reports the failure, and after a reboot the keepalive may hold the MAC with a poll.
- The rejoin series starts from the timer with delay 0, outside the NWK callback that reports the cause.
- BDB holds the fast poll rate with `ZZB_NWK_HOLD_BDB` in the states that wait for frames from the network, the network key after a join or a trust center rejoin and the steps of the trust center link key exchange, and while finding & binding runs as target or initiator (BDB 6.8). The pause between rejoin series holds nothing. One function sets the state and recomputes the hold.
- A callback that does not belong to the current step is ignored: a repeated Confirm Key, a Node_Desc_req confirm after its step, the network key of a key update on the network, and a trust center link key that BDB did not request (acceptNewUnsolicitedTrustCenterLinkKey FALSE, BDB 10.2.6).

### Initialization and rejoin

- `zzb_bdb_start()` reports INIT with NOT_ON_A_NETWORK on a node off a network. On a network it runs the rejoin series with event INIT (BDB 7.1).
- The rejoin series uses the extended PAN ID of the network:
  1. secure rejoin with channels 0 and duration 0 (BDB 7.1 step 4);
  2. secure rejoin on channels 0x07fff800 with bdbScanDuration;
  3. trust center rejoin on channels 0x07fff800 with bdbScanDuration, then a wait of apsSecurityTimeOutPeriod for the network key.
- A synchronous error other than `-EBUSY` or a confirm other than SUCCESS passes to the next step. A success sends Device_annce (BDB 7.1 step 5), after a trust center rejoin when the key has arrived, and reports the event with SUCCESS.
- After step 3 fails, the series repeats after 30 s. The pause doubles up to 15 min and returns to 30 s after a success. A network key that arrives during the pause after a trust center rejoin is a success, since NWK stayed with the parent with the key pending.
- On a node on a network, with no BDB procedure running, the series with event REJOIN starts at:
  - NLME-NWK-STATUS.indication with parent link failure;
  - NLME-NWK-STATUS.indication with address conflict for the address of the node; an end device rejoins to get a new address (R22 3.6.1.9.3);
  - a leave with rejoin.
- NLME-NWK-STATUS.indication with network address update for the address of the node sends Device_annce (R22 3.6.1.9.2).
- The keepalive detects the loss of the parent (section 10). With nwkParentInformation 0 only failed transmissions of the application detect it.

### Network steering

- On a network (BDB 8.1): Mgmt_Permit_Joining_req with bdbcMinCommissioningTime and TC significance 1, then STEERING with SUCCESS. NLME-PERMIT-JOINING (step 4) does not apply to an end device.
- Off a network (BDB 8.2):
  - Steps 2 to 4: NLME-NETWORK-DISCOVERY on vScanChannels with bdbScanDuration. An error or a status other than SUCCESS continues at step 13.
  - Steps 5 to 8: the networks with permit joining, in the order of the descriptors, except the networks in the list of failed networks. NLME-JOIN uses association, channels 0, duration 0 and no security. A synchronous error or a failed confirm passes to the next network. Each network is tried once, since NWK tries every parent of the network.
  - Step 9: a wait of apsSecurityTimeOutPeriod for `zzb_bdb_nwk_key_ind()`.
  - Step 10: when the key does not arrive, NLME-RESET and `zzb_aps_reset()` follow, and the extended PAN ID enters the list of failed networks (`CONFIG_ZZB_NWK_NETWORK_COUNT` entries, cleared when steering starts). Step 3 repeats on the same channels, since the association and NLME-RESET removed the other networks from the neighbor table. A full list continues at step 13.
  - Step 11: bdbNodeIsOnANetwork, Device_annce, then the key exchange. The check of apsTrustCenterAddress against 0xffffffffffffffff is left out: APS drops a network key with that source, ZDO sets the trust center address before the key, and distributed security is excluded (section 1).
  - Step 12: a failed key exchange leaves without rejoin, resets the node and reports STEERING with TCLK_EX_FAILURE.
  - Step 13: the secondary set, when it is not 0 and has not been scanned.
  - Steps 14 to 16: Mgmt_Permit_Joining_req as in BDB 8.1, then STEERING with SUCCESS. Step 15 does not apply to an end device.
  - Step 17: STEERING with NO_NETWORK.

### Trust center link key exchange

BDB 10.2.5 with method 0:

- Steps 2 to 4: Node_Desc_req to 0x0000, at most 3 attempts. The confirm of the ZDO client is the timer (TIMEOUT after 5 s, section 12). A status other than SUCCESS is a failed attempt, and so is a synchronous error, which arms the BDB timer for 5 s.
- Step 5: a stack compliance revision (bits 9 to 15 of the server mask) below 21 ends the exchange with success. Z-Stack Home 1.2 reports 0.
- Steps 6 to 8: APSME-REQUEST-KEY and a timer of 5 s, at most 3 attempts.
- Step 9: a trust center link key equal to the key of the trust center entry fails the exchange. Another key replaces it with `zzb_apsme_set_link_key()`, unverified and unique.
- Steps 10 to 12: APSME-VERIFY-KEY and a timer of 5 s, at most 3 attempts. Confirm Key with SUCCESS and the trust center link key type ends the exchange with success.
- A trust center rejoin does not repeat the exchange.

### Finding & binding

Both procedures run beside the procedures above and do not make them return `-EBUSY`. An initiator that was a BDB procedure would block the rejoin series after a parent link failure (Initialization and rejoin), and it waits for ZDO responses for tens of seconds. Both end with `zzb_bdb_notify()`, since `zzb_bdb_report()` ends the BDB procedure in progress. Each public function marshals its own request struct with `zzb_call_sync()`.

Target (BDB 8.4), `zzb_bdb_finding_binding_target(node, ep)`:

- Off a network the event comes with NOT_ON_A_NETWORK. Without an Identify server on ep the function returns `-ENOENT`, and while a target procedure runs `-EBUSY`.
- IdentifyTime of the Identify server on ep becomes bdbcMinCommissioningTime (180 s) through `zzb_zcl_identify()`, with `zcl_cb`. Other identifying endpoints are not changed (MAY in step 3).
- The Identify server calls `zzb_bdb_identify_end(node, ep)` whenever its IdentifyTime becomes 0: by the countdown, an Identify command or a write. For the endpoint of the target this ends the procedure with SUCCESS.

Initiator (BDB 8.5), `zzb_bdb_finding_binding_initiator(node, ep, group)`:

- group `ZZB_BDB_FB_UNICAST` (0xffff) makes unicast bindings, 0x0001 to 0xfff7 group bindings; another value returns `-EINVAL`. Without an Identify client instance on ep the function returns `-ENOENT`; the instance must be `ZZB_ZCL_IDENTIFY_CLIENT()`, whose definition passes Identify Query Response to BDB. While an initiator runs, `-EBUSY`. Off a network the event comes with NOT_ON_A_NETWORK.
- Step 3: one Identify Query from ep to 0xffff and endpoint 0xff.
- Step 4: `zzb_bdb_identify_query_rsp(node, ep, addr, src_ep)` adds the respondents for 5 s, up to `CONFIG_ZZB_BDB_FB_RESPONDENT_COUNT` (default 4); further responses are ignored. Without a respondent the event comes with NO_IDENTIFY_QUERY_RESPONSE.
- Steps 5 and 6: Simple_Desc_req to each respondent through the ZDO client; an error status, a timeout or a synchronous error continues with the next respondent.
- Step 7: a respondent with another profile than ep has no match. A cluster matches when ep has a client instance and the respondent lists it as input cluster, or ep has a server instance and the respondent lists it as output cluster. Identify, which serves the procedure itself, is not bound, and neither are IAS Zone, IAS ACE and IAS WD, for which the DTL forbids finding & binding (DTL 43.3.1, 44.3.1, 45.3.1, 46.3.1). The stack does not record whether a cluster is of type 1 or 2 (BDB 6.5), so every complementary pair is bound.
- Step 8: for unicast bindings, IEEE_addr_req to the respondent once per address, which also sets its nwkAddressMap entry; then APSME-BIND with the IEEE address and the endpoint of the respondent. For group bindings, APSME-BIND with the group. A failed IEEE_addr_req continues with the next respondent. A full binding table ends the procedure with BINDING_TABLE_FULL.
- Step 10: after a group binding, one Add Group If Identifying from ep to 0xffff and endpoint 0xff with the group and an empty name. The broadcast form needs no response handling.
- Step 11: the event comes with SUCCESS. Identify with IdentifyTime 0 to the targets (MAY) is not sent.

### Reset and leave

- The reset of the node:
  - NLME-RESET, which deletes the NWK records;
  - `zzb_aps_reset()`;
  - `zzb_zdo_init()`, which clears the transactions of the ZDO client and a pending channel change of Mgmt_NWK_Update_req;
  - `zzb_zcl_reset()`, which restores the defaults of the ZCL attributes and deletes their records (section 16);
  - bdbNodeIsOnANetwork FALSE, the BDB procedure and timer cancelled.
  - finding & binding for a target and for an initiator cleared without an event, and the timer of the initiator cancelled, before `zzb_zcl_reset()`, whose reset of IdentifyTime would otherwise end the target with SUCCESS;
  - The frame counter records stay (BDB 9).
- `zzb_bdb_reset()` (BDB 9.5) on a node on a network issues NLME-LEAVE without rejoin, and its confirm resets the node and reports LEFT.
  - `-EBUSY` from NLME-LEAVE is returned and changes nothing: NWK runs a procedure, such as a leave requested by the parent, which ends with its own indication.
  - Another error (no parent, no key) or a node off a network resets the node at once and reports LEFT.
- NLME-LEAVE.indication for the node itself (BDB 9.3): without rejoin the node is reset and LEFT follows, with rejoin the series with event REJOIN starts. An indication about a neighbor is ignored; NWK reports a lost parent with parent link failure.
- Mgmt_Leave_req (BDB 9.4): ZDO calls `zzb_bdb_leave(node, rejoin)` after the response. BDB cancels its procedure and issues NLME-LEAVE, again after 1 s on `-EBUSY`. The confirm, or another error at once, resets the node and reports LEFT without rejoin, or starts the series with event REJOIN with rejoin. BDB issues the leave because an NLME-LEAVE.request that fails produces no confirm, and the reset would not happen.

## 14. Security and frame counters

`sec/` contains pure functions on top of PSA Crypto:

- AES-128-ECB (`PSA_WANT_ALG_ECB_NO_PADDING`) for the Matyas-Meyer-Oseas hash (R22 Annex B.6). Only the padding for messages shorter than 2^16 bits is implemented; Zigbee hashes at most 32 bytes;
- AES-CCM with a 4-byte tag, `PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4)`, for security level 5 (ENC-MIC-32), the only level used on air. The CCM primitive takes the MIC length, so the R22 Annex C vectors (M=8) apply to it. The frame functions on top of it parse the auxiliary header, build the nonce and apply the level-field rule (section 7);
- HMAC-MMO with a 16-byte key and a 1-octet input, the only form R22 uses: key-transport key (0x00), key-load key (0x02), verify-key hash (0x03) (R22 4.5.3, 4.4.10.7.4, Annex B.1.4);
- install code CRC (CRC-16/X-25 with Zephyr `crc16_ccitt()`, BDB 10.1.1) and install code key derivation (BDB 10.1.2).

CCM functions take a `psa_key_id_t`. The PSA key cache in `sec/key_cache.c` serves all nodes: `zzb_sec_key_get(key, &id)` returns the ID of an imported key with the same 16 bytes, or imports the key with the policy `PSA_ALG_AEAD_WITH_AT_LEAST_THIS_LENGTH_TAG(PSA_ALG_CCM, 4)` into the least recently used of `CONFIG_ZZB_KEY_CACHE_SIZE` entries (default 4). The ID is valid until the next call on the stack thread. Tables store the 16 key bytes. Zephyr builds PSA with `MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS`, under which input and output buffers must not overlap, so in-place CCM copies its input to a local buffer first. After a MIC mismatch the decryption copies the ciphertext back, so a caller can try another key on the same frame. The auxiliary header codec writes the security level sub-field as 0; the frame functions set it to 5 for the CCM operation.

Keys are stored as arrays: two network keys and N link-key pairs. The key derived from the install code is kept by the node, outside the APS state that `zzb_aps_init()` clears at a reboot; it is not saved, since the application passes the install code to every `zzb_node_init()`, and `zzb_aps_reset()` keeps it. The trust center is identified by `apsTrustCenterAddress` (IEEE address). Code must not assume that the trust center is the parent. Frames to the trust center go to network address 0x0000, since R22 4.6.2.2 places the trust center on the coordinator in a centralized network.

Outgoing frame counters: one for NWK and one for APS, global to the node, both implemented by `common/fc.c`. The NWK counter must survive factory reset, leave and joining another network (R22 4.3.4). Both are persisted as a watermark under `zzb/<n>/fc/nwk` and `zzb/<n>/fc/aps`: a version byte (1) followed by the watermark, little-endian, 4 bytes. The reset of the node (section 13) erases `zzb/<n>/nwk`, `zzb/<n>/aps` and `zzb/<n>/zcl` and keeps `zzb/<n>/fc`.

- On boot: counter = watermark, then `zzb_node_init()` saves watermark + `CONFIG_ZZB_FRAME_COUNTER_JUMP` (default 1024). It returns the error if the save fails or the record has an unknown version or length.
- At runtime: when the counter reaches watermark - JUMP/2, a work item on the stack queue saves counter + JUMP and moves the watermark when the save succeeds. After a failed save, the next secured frame schedules the save again. While the counter equals the watermark, securing a frame fails with `-EBUSY`; a counter of 2^32-1 fails with `-EOVERFLOW` (R22 4.3.1.1 step 1).

Incoming frame counters are kept in RAM, in the neighbor table (section 10) and the key-pair table (section 11).

## 15. Persistence

Zephyr settings: NVS on flash parts (nRF52840, ESP32-C6/H2), ZMS on RRAM parts. Each record has its own key and a version field, since writes are atomic per key. Tables that grow on a trust center store one key per entry.

Persisted records:

- NWK: PAN ID, extended PAN ID, short address, channel, nwkUpdateId, parent, network keys and key sequence number, nwkAddressMap;
- APS: link keys, trust center address, binding table, group table;
- ZCL: non-volatile attributes, reporting configuration, scene table, alarm table, weekly schedules, the endpoints of IAS CIEs and the users of Door Locks;
- frame counter watermarks.

bdbNodeIsOnANetwork (BDB 6.9) follows from the record `nwk/net` (section 13).

ZHA writes bindings and reporting configuration once, during pairing. Losing them on reboot stops attribute reports without an error on either side.

NWK records, little-endian:

| Key | Contents | Length |
|---|---|---|
| `zzb/<n>/nwk/net` | version (1), PAN ID, extended PAN ID, network address, channel, nwkUpdateId, nwkCapabilityInformation, nwkParentInformation, network address, IEEE address and device type of the parent | 28 |
| `zzb/<n>/nwk/keys` | version (1), active slot, slot written last, two slots of valid flag, key sequence number and key | 39 |
| `zzb/<n>/nwk/addr/<i>` | version (1), IEEE address and network address of nwkAddressMap entry i | 11 |

- NWK writes its records only while the node is on a network and the key is not pending: when a key ends the pending state, after a secure rejoin, when nwkParentInformation or the address changes, when a key is set on the network and when a received frame switches the active key. Save errors are ignored; the next save writes the whole record again.
- The loader writes the NIB and the key slots directly, adds the parent entry with the extended PAN ID, PAN ID, channel and device type, calls MLME-SET for the PAN ID, the address and the channel, and starts the keepalive. A record with an unknown version or length makes `zzb_node_init()` return `-EINVAL`.
- `zzb_nlme_set_addr_map()` saves an address map entry.
- A leave without rejoin and NLME-RESET delete the records.

APS records, little-endian:

| Key | Contents | Length |
|---|---|---|
| `zzb/<n>/aps/tc` | version (1), apsTrustCenterAddress | 9 |
| `zzb/<n>/aps/key/<i>` | version (1), device address, key attributes, link key type and link key of key-pair entry i | 27 |
| `zzb/<n>/aps/bind/<i>` | version (1), source endpoint, cluster, destination address mode, destination address (8 bytes, a group address in the first two) and destination endpoint of binding entry i | 14 |
| `zzb/<n>/aps/group/<i>` | version (1), group address and endpoint of group table entry i | 4 |

- APS saves the trust center address in `zzb_apsme_set_tc_addr()`, and an entry when `zzb_apsme_set_tc_addr()` or `zzb_apsme_set_link_key()` adds or changes it or a Confirm Key verifies it. `zzb_apsme_bind_req()` saves a binding entry, `zzb_apsme_unbind_req()` deletes it, and the group functions save and delete group entries. Save errors are ignored. `zzb_aps_reset()` deletes the records.
- A record with an unknown version or length makes `zzb_node_init()` return `-EINVAL`.

ZCL records, little-endian:

| Key | Contents | Length |
|---|---|---|
| `zzb/<n>/zcl/<ep>/<cluster>/<attr>` | version (1) and the value of a non-volatile attribute in its ZCL encoding; the endpoint in decimal, the cluster and attribute IDs in 4 hexadecimal digits | 1 + value |
| `zzb/<n>/zcl/rep/<i>` | version (1), endpoint, cluster, attribute, minimum interval, maximum interval and reportable change of reporting table entry i | 12 |
| `zzb/<n>/zcl/scene/<i>` | version (1), endpoint, group, scene, transition time, TransitionTime100ms and the extension field sets of scene table entry i | 8 + sets |
| `zzb/<n>/zcl/alarm/<i>` | version (1), sequence number, endpoint, alarm code, cluster and time stamp of alarm table entry i | 13 |
| `zzb/<n>/zcl/sched/<ep>/<day>` | version (1), mode, number of transitions and the transitions (time, heating and cooling setpoint) of day 0 to 7 of the weekly schedule of the Thermostat server on ep | 3 + 6 per transition |
| `zzb/<n>/zcl/ias/<ep>` | version (1) and the endpoint of the IAS CIE of the IAS Zone server on ep | 2 |
| `zzb/<n>/zcl/lock/<ep>/<user>` | version (1), user status, user type, code length and 8 octets of code of a user of the Door Lock server on ep, the user ID in decimal | 12 |

- ZCL saves a non-volatile attribute at every change, except during a transition (section 16), a reporting table entry at Configure Reporting, a scene table entry, an alarm table entry and a day of the weekly schedule at every change of them, the endpoint of the IAS CIE at a write of IAS_CIE_Address, a user of a Door Lock at every change of it, and deletes removed entries, empty days and cleared users. Save errors are ignored. `zzb_zcl_reset()` deletes the records.
- The loader recognizes `zcl/rep/<i>`, `zcl/scene/<i>`, `zcl/alarm/<i>`, `zcl/sched/<ep>/<day>`, `zcl/ias/<ep>` and `zcl/lock/<ep>/<user>` before the attribute records.
- A record with an unknown version or length makes `zzb_node_init()` return `-EINVAL`, and so do a day of the weekly schedule with more transitions than the capacity and a user of a Door Lock with a code longer than 8 octets. A record of an endpoint, cluster or attribute that the device definition does not have is ignored, and so are a weekly schedule record of a day above 7 and a Door Lock record of a user outside the table. An alarm record of 9 octets, written before milestone 17, loads with the time stamp 0xffffffff.
- The OTA Upgrade client (section 16) has no records. After a reboot ImageUpgradeStatus is Normal and a download starts again at offset 0; an upgrade time that was counting down is lost, which ZCL 11.16 permits. An upgrade that `boot_request_upgrade()` has requested is kept by MCUboot in the trailer of the upload slot.
- Settings keys are built in a 32-byte buffer; `zzb/<n>/zcl/254/0006/4003` has 23 characters for n below 10.

## 16. ZCL

`CONFIG_ZZB_ZCL` builds `zcl/` and selects `CONFIG_SETTINGS`. An NCP build leaves ZCL to the host. Milestone 12 implements the commands that read, write and discover attributes, the Default Response, and the Basic, Identify and On/Off servers with the attributes and commands that the DTL requires for the On/Off Light. Milestone 13 adds attribute reporting and the Groups and Scenes servers, which complete the On/Off Light, and the Identify client of finding & binding. Milestone 14 adds the servers of the lighting device types and controllers of the DTL (Level Control for Lighting, Color Control, Ballast Configuration, Power Configuration, Device Temperature Configuration, Illuminance Measurement, Illuminance Level Sensing, Occupancy Sensing), the Alarms server, client definitions and `zzb_zcl_send_bound()`. Milestone 15 adds the servers of the closure and HVAC device types (Window Covering, Thermostat with its weekly schedule, Thermostat User Interface Configuration, Fan Control, Pump Configuration and Control, Temperature, Pressure, Flow and Relative Humidity Measurement), their client definitions and `zcl_cmd_cb`. Milestone 16 adds the clusters of the intruder alarm system device types: the IAS Zone server with enrollment and test mode, the IAS WD server, the IAS ACE server and client, and the IAS Zone and IAS WD clients of the CIE, whose logic is in the application; the enum16 and EUI64 types, `zzb_zcl_cmd_respond()` and commands to an IEEE address. Milestone 17 adds the clusters of the generic device types: On/Off Switch Configuration, Door Lock with PIN users, Binary Input, Time with the clock of the node, Metering, Meter Identification, Electrical Measurement, Power Profile and the EN50523 appliance clusters, whose logic is in the application; values of up to 64 bits with the int24, int32, UTC, uint48 and uint56 types, and the `refresh` hook. Milestone 18 adds the Poll Control server and client and the fast poll after the requests of the stack. Milestone 19 adds the OTA Upgrade client with `CONFIG_ZZB_ZCL_OTA`.

| File | Contents |
|---|---|
| `zcl.c` | init, start, reset, reception, lookup of endpoints and clusters, responses, frames to bound destinations, timers |
| `frame.c` | ZCL header codec, data type lengths and the length check of command fields |
| `attr.c` | attribute lookup and value encoding, `zcl_cb`, records, `zzb_zcl_attr_set()` |
| `foundation.c` | global commands |
| `report.c` | reporting table, Configure Reporting, Read Reporting Configuration, Report Attributes |
| `transition.c` | transitions of Level Control and Color Control |
| `clusters/` | `basic.c`, `identify.c`, `on_off.c`, `groups.c`, `scenes.c` with the scene table, `level.c`, `color.c`, `alarms.c` with the alarm table, `power_config.c`, `device_temp.c`, `ballast.c`, `illuminance.c`, `occupancy.c`, `window_covering.c`, `thermostat.c`, `schedule.c` with the weekly schedule, `thermostat_ui.c`, `fan.c`, `pump.c`, `measurement.c`, `ias_zone.c`, `ias_wd.c`, `ias_ace.c`, `on_off_switch_config.c`, `door_lock.c` with the PIN users, `binary_input.c`, `time.c` with the clock of the node, `metering.c`, `meter_id.c`, `electrical_measurement.c`, `power_profile.c`, `appliance_control.c`, `appliance_id.c`, `appliance_events.c`, `appliance_stats.c`, `poll_control.c`, `ota.c` with `CONFIG_ZZB_ZCL_OTA` |

### Data model

- `include/zzb/zcl.h` declares the attribute descriptor, the cluster definition, the cluster instance, the callbacks and `zzb_zcl_attr_set()`. `include/zzb/zcl/<cluster>.h` declares the value struct and the instance macro of a cluster of the stack. `zzb.h` includes `zzb/zcl.h`.
- An application needs no number from ZCL. Each cluster header names the coded values of the attributes of its cluster: an enum per enumeration, `enum zzb_zcl_<cluster>_<attribute>` with the values `ZZB_ZCL_<CLUSTER>_<ATTRIBUTE>_<VALUE>`, and a `BIT()` define per bit of a bitmap, `ZZB_ZCL_<CLUSTER>_<ATTRIBUTE>_<BIT>`. The special values of an attribute, such as an unknown measured value or the start-up value that keeps the value before the power-down, have names of the same form. The names of the earlier headers, such as `ZZB_ZCL_START_UP_PREVIOUS` and `ZZB_ZCL_SYSTEM_MODE_HEAT`, stay. Physical quantities are numbers in the unit of the attribute, which the comment of the field states.
- Static inline functions in the cluster headers build the payload of a command from typed arguments: a client command goes to the bound destinations through `zzb_zcl_send_bound()`, a response within `zcl_cmd_cb` through `zzb_zcl_cmd_respond()`. Each function has the name of the identifier of its command in lower case, e.g. `zzb_zcl_level_move_to_level_on_off()` for `ZZB_ZCL_LEVEL_MOVE_TO_LEVEL_ON_OFF`, and returns the result of the function it calls. Commands without a payload, such as Toggle, need none.
- A command that the application receives in `zcl_cmd_cb` and reads has a struct of its fields in the cluster header, `struct zzb_zcl_<command>`, and a static inline function `zzb_zcl_<command>_parse(cmd, &fields)` that reads them from the payload, with the same command name as above. The stack has checked the length of the fixed fields before `zcl_cmd_cb`, so the function returns nothing; an optional field after them that the payload lacks gets the default of ZCL.
- An attribute descriptor holds the attribute ID, the data type, flags and the offset of the field in the value struct. The flags are read, write and report access (ZCL 2.3.4.4), constant and non-volatile. A constant attribute, such as ClusterRevision or ZCLVersion, holds its value in place of the offset. `ZZB_ZCL_ATTR(id, type, flags, struct, field)` checks the size of the field against the type with `ZERO_OR_COMPILE_ERROR()`, since `BUILD_ASSERT` cannot stand in an initializer. `ZZB_ZCL_ATTR_CONST(id, type, value)` declares a constant. Attribute tables are in ascending order of attribute ID.
- Storage types: bool, map8, uint8, int8 and enum8 in one byte, uint16, map16, int16 and enum16 in two, uint24, uint32, map32 and UTC in a `uint32_t`, int24 and int32 in an `int32_t`, uint48, uint56 and EUI64 in a `uint64_t`, a character string as `const char *` and an octet string as `const uint8_t *` pointing at its length octet. Strings are read-only, and NULL reads as an empty string. Other types are added with the clusters that use them.
- Values pass through the stack as `uint64_t` with the bits of the field. An int8 or int16 is not sign-extended; an int24 is sign-extended to the 32 bits of its field when it is written or loaded, since otherwise -1 would be stored as 0x00ffffff. The length of a value on air, in a settings record and in a scene is the length of its ZCL type, which differs from the size of the field for uint24, int24, uint48 and uint56. Constants have 16 bits. The internal setters `zzb_zcl_set()`, `zzb_zcl_store()` and `zzb_zcl_update()` and the `check` hook take `uint32_t`: an attribute wider than 32 bits (uint48, uint56, EUI64) has no `check` and is not in a scene extension field set. An EUI64 is not reportable.
- A cluster definition holds the attribute table, the attributes of its scene extension field set (Scenes) and optional hooks: `init` sets the defaults before the records are loaded, `start` runs after loading, `reset` restores the defaults at the reset of the node, `cmd` processes a cluster-specific command, `check` validates a remote write, `changed` follows every change of a value, `recall` applies the extension field set of a recalled scene, `alarm_reset` raises the alarms of the cluster again (Alarms), and `refresh` updates the values that the cluster computes before Read Attributes reads them and before a write checks them (Time). The attributes that `refresh` computes are not reportable, not non-volatile and not in a scene, so they need no timer. The stack defines its clusters; an application can define its own.
- `check(values, attr, value)` gets the value struct, so that a limit can depend on another attribute. It returns SUCCESS, INVALID_VALUE or READ_ONLY; READ_ONLY refuses a write of an attribute that ZCL makes writable under a condition (R*W), such as PresentValue of Binary Input while OutOfService is FALSE. The hook checks the range of the value before the condition. Write Attributes checks every record against the values before the command, so one command that sets MinLevel and MaxLevel of Ballast Configuration together can get INVALID_VALUE although the result is valid, and SUCCESS although MinLevel ends above MaxLevel, and one command that sets OutOfService TRUE and PresentValue of Binary Input gets READ_ONLY for PresentValue.
- A cluster instance holds the cluster ID, server or client, the value struct and the definition, which is NULL for a cluster without attributes and commands. The definition is a separate object because a macro in a public header cannot take the size of a table defined in a source file. `ZZB_ZCL_ON_OFF_SERVER(&values)` and the macros of the other clusters build an instance and check the type of the value struct.
- An endpoint lists its cluster instances. Its simple descriptor has the server instances as input clusters and the client instances as output clusters (section 12).
- `struct zzb_zcl_cmd`, which the `cmd` hook receives, tells whether the command came as a NWK unicast to the endpoint, so that the hook sends the responses that ZCL allows only for unicast, and with `server` whether it addresses the server instance (direction 0).
- While a frame is processed, the internal field `node->zcl.received` points at its `struct zzb_zcl_cmd`, so that a hook can tell the source of a write (IAS Zone); it is NULL for a change by `zzb_zcl_attr_set()`, a timer or the reset of the node.
- The value structs belong to the application and are written only on the stack thread. The application reads them in the callbacks and changes them with `zzb_zcl_attr_set()`, which runs on the stack thread through `zzb_call_sync()`. Its value has the type of the field: `uint8_t`, `int8_t` or `bool`, `uint16_t` or `int16_t`, `uint32_t` for uint24, uint32, map32 and UTC, `int32_t` for int24 and int32, `uint64_t` for uint48 and uint56. It returns `-ENOENT` for an unknown endpoint, cluster or attribute and `-EINVAL` for a constant, a string, an EUI64, a value outside the range of a uint24, int24, uint48 or uint56, or a value that `check` rejects with INVALID_VALUE; READ_ONLY from `check` does not apply to the application. It saves a non-volatile attribute and calls `changed`.
- `zcl_cb(node, ep, cluster, attr)` in `struct zzb_node_config` is called on the stack thread when a remote command, a write, a timer of the stack or the reset of the node gives an attribute a new value. `zzb_zcl_attr_set()` and `zzb_node_init()` do not call it, and the values that the stack derives from a value set by `zzb_zcl_attr_set()` change without it: SceneValid, BatteryAlarmState, MinTempExperienced, MaxTempExperienced and AlarmCount. The clusters that raise alarms tell the source of a change by the attribute ID in the `changed` hook: a measurement that the application sets (MainsVoltage, BatteryVoltage, BatteryPercentageRemaining, CurrentTemperature, LampBurnHours) changes BatteryAlarmState and AlarmCount without `zcl_cb`, an alarm setting changes them with it, also when the application sets the setting with `zzb_zcl_attr_set()`. LampBurnHours counts as a measurement also when a write sets it. CurrentX and CurrentY, which follow ColorTemperatureMireds, and ColorTemperatureMireds, which follows CurrentLevel (Color Control), change with it through `zzb_zcl_update()`, before the `zcl_cb` of the value they follow; ColorTemperatureMireds also when StartUpCurrentLevel changes CurrentLevel in `zzb_node_init()`.
- `zcl_effect_cb(node, ep, cluster, effect, variant)` receives the fields of Trigger Effect and Off With Effect as received, before the `zcl_cb` calls of the same command. The application uses the default variant for a variant it does not support (ZCL 3.5.2.3.3.2, 3.8.2.3.4.2).
- `zcl_cmd_cb(node, cmd)` in `struct zzb_node_config` receives, on the stack thread, the cluster-specific commands that the application carries out, after the checks of the stack, and returns the status of the Default Response; without it they get SUCCESS. These are the commands of Window Covering and its recalled scenes, of the IAS WD server, of the IAS ACE server and client, of the IAS Zone client, Lock Door and Unlock Door of the Door Lock server, Check-in of the Poll Control client, and the commands of the Power Profile, Appliance Control, Appliance Events and Alerts and Appliance Statistics servers and clients. The payload is valid during the call.
- The clusters that pass commands to `zcl_cmd_cb` check their length with a table of fields: `zzb_zcl_fields_len()` of `frame.c` takes fixed fields and counted lists, whose count is an octet or its low nibble, and returns the length of the fields, so that an optional field after them can be seen; `zzb_zcl_pass()` of `zcl.c` finds the command in a table, checks its fields, gives MALFORMED_COMMAND to a shorter payload and UNSUP_COMMAND to a command missing from the table, and passes the command to `zcl_cmd_cb`. The cluster files check the fields that a table cannot describe: the 4-octet log length of Appliance Statistics, the typed records of Write Functions, PowerProfileStartTime of GetPowerProfilePriceExtended, which bit 0 of Options announces, and the code of Door Lock.
- Within `zcl_cmd_cb` the application answers a command that came as unicast with `zzb_zcl_cmd_respond(node, cmd, id, payload, len)`, which sends the cluster-specific command id with the TSN of the command to its source, in the opposite direction, as the responses of the stack. Then no Default Response follows (ZCL 2.5.12.2), and the status that `zcl_cmd_cb` returns is ignored. For another command, such as one that the stack builds, it returns `-EINVAL`. The internal function that the clusters of the stack use for their responses, `zzb_zcl_cmd_reply()`, also answers broadcasts (Identify Query, Get Group Membership, Get Scene Membership, Get Alarm).
- The cluster tables are written by hand from ZCL rev 8. They may later be generated from the project-chip/zap ZCL XML (`zcl-builtin/silabs`, Apache-2.0) and checked against zigbee-herdsman `src/zspec/zcl` (MIT).

### Reception

- ZDO passes group frames and frames with a destination endpoint other than 0 to `zzb_zcl_data_ind()`. ZCL accepts an endpoint of the device definition with the profile of the endpoint or 0xffff, and the broadcast endpoint 0xff, whose frame goes to every endpoint with the cluster (ZCL 2.3.2.1). A group frame goes to every endpoint that is a member of the group and has the cluster; like a frame to 0xff, it gets no Default Response. Other frames, frames shorter than the ZCL header and frames with a reserved frame type are dropped.
- Direction 0 addresses a server instance, direction 1 a client instance.
- The Default Response (ZCL 2.5.12.2) answers only a NWK unicast to an endpoint other than 0xff (ZCL 2.3.2.1), and never a received Default Response. It carries SUCCESS when Disable Default Response is 0, and an error status in every case:
  - UNSUPPORTED_CLUSTER (0xc3) when the endpoint has no instance for the cluster and the direction;
  - UNSUP_COMMAND (0x81) for a manufacturer-specific frame, with the manufacturer-specific bit and the received manufacturer code (ZCL 2.5.12.1.1), and for an unknown global or cluster-specific command, including received responses to a server instance and a received Report Attributes on a server instance. Client instances accept a Report Attributes, the Time client a Read Attributes Response (Time), and the client definitions of the stack except IAS Zone, IAS ACE and OTA Upgrade and those that pass their commands to `zcl_cmd_cb` any cluster-specific command (Clients);
  - MALFORMED_COMMAND (0x80) for a payload shorter than its fixed fields. Octets after them are ignored (ZCL 2.3.2).
- Read, Write and Discover Attributes responses and Identify Query Response are also sent for broadcasts; item 3 of ZCL 2.3.2.1 is read as excluding error responses only. Write Attributes No Response gets no frame (ZCL 2.5.6.3).
- A response has Disable Default Response 1, the opposite direction and the TSN of the request. It goes to the network source and the source endpoint of the request, from the endpoint that processed it and with the profile of that endpoint. It has NWK security only, an APS ACK request as unicast, as ZDO frames (section 12), and ASDU handle 0xfd, whose confirm is ignored. Requests with APS security are processed, and their responses go without it. The buffer reserves the headroom of a ZDO frame, so an ASDU holds up to `ZZB_APS_MAX_ASDU` (82) bytes; `zzb_zcl_send()` returns `-EMSGSIZE` for a longer frame, since `zzb_zcl_send_bound()` and `zzb_zcl_cmd_respond()` take payloads from the application.
- Frames that the stack originates use the same path. Their TSN comes from a counter of the node with a random initial value. `zzb_zcl_send_cmd(node, src_ep, dst_addr, dst_ep, cluster, cmd, payload, len)` sends a cluster-specific command from a client with Disable Default Response 1 and an APS ACK request as unicast, for finding & binding (section 13); it returns `-ENOENT` for an unknown source endpoint and the errors of APSDE-DATA.request. `zzb_zcl_send_ext(node, ep, dst_ext, dst_ep, cluster, cmd, payload, len)` sends a cluster-specific command of a server to an IEEE address and endpoint with APS address mode 0x03, Disable Default Response 1 and an APS ACK request, for IAS Zone; APS resolves the address through the trust center address, the neighbor table and nwkAddressMap (section 11).
- A command that its effect on receipt discards (Identify Query while not identifying, On With Recall Global Scene with GlobalSceneControl TRUE, On With Timed Off refused by Accept Only When On) follows the general rule and gets SUCCESS when Disable Default Response is 0. zigpy waits for the Default Response of a cluster command.

### Foundation commands

- Read Attributes (ZCL 2.5.1.3): an unknown attribute gets UNSUPPORTED_ATTRIBUTE. A record whose value does not fit gets INSUFFICIENT_SPACE in 3 bytes, and the next attributes are still processed; the response ends where 3 bytes do not fit. An odd last octet is ignored.
- Write Attributes (ZCL 2.5.3.3) first parses every record by the length of the type in the record. A truncated record or a type of unknown length gives MALFORMED_COMMAND, and nothing is written. The checks run in the order UNSUPPORTED_ATTRIBUTE, INVALID_DATA_TYPE, READ_ONLY, INVALID_VALUE (a bool other than 0 and 1, a value that `check` rejects). The response lists the status and attribute ID of each failed record, or one SUCCESS record without an ID.
- Write Attributes Undivided (ZCL 2.5.4) checks every record; when one fails, nothing is written and the response lists the failed records. Write Attributes No Response (ZCL 2.5.6) writes the records that pass the checks. `zcl_cb` is called after all records are written.
- Discover Attributes (ZCL 2.5.13) lists the attributes from the start ID, at most the requested number and 26, the number of records that fit in 82 bytes. Discovery complete is 1 when no attribute follows the last one listed.
- The data type lengths cover the fixed-length types of ZCL Table 2-11 and the four string types, so a write record of any attribute can be skipped.
- Not implemented: Discover Commands, Discover Attributes Extended, structured read and write (no attribute has a structured type), manufacturer-specific attributes and commands.

### Reporting

- The reporting table has `CONFIG_ZZB_ZCL_REPORT_COUNT` entries (default 16, since ZHA 2.3.1 configures 11 reports on a Thermostat): endpoint, cluster, attribute, minimum and maximum interval, reportable change, the value of the last report, the time of the last report and whether a change waits. One `k_work_delayable` serves the table.
- The value of the last report has 64 bits and the reportable change 16. A configured change above 0xffff, which a uint32, int32, uint48 or int24 can carry, is stored as 0xffff: the attribute reports at a smaller change than configured, and Read Reporting Configuration returns 0xffff. The record `zcl/rep/<i>` keeps its format. An int8, int16, int24 or int32 is compared with its sign.
- Configure Reporting (ZCL 2.5.7):
  - A record is parsed by the length of its own data type. A truncated record gives MALFORMED_COMMAND, and nothing is configured.
  - Direction 0x01 (reports received) gives UNREPORTABLE_ATTRIBUTE: no cluster of the stack receives reports.
  - The checks run in the order UNSUPPORTED_ATTRIBUTE, UNREPORTABLE_ATTRIBUTE (an attribute without the report flag), INVALID_DATA_TYPE, INVALID_VALUE (a maximum interval other than 0 below the minimum interval).
  - The reportable change is present for the analog types of ZCL Table 2-11 with the length of the type and absent for the discrete and composite types.
  - A maximum interval of 0xffff, or 0 with a minimum interval of 0xffff, removes the configuration; the stack has no default configuration to revert to.
  - A full table gives INSUFFICIENT_SPACE; ZCL defines no status for this case.
  - The response lists the status, direction and attribute ID of each failed record, or one SUCCESS record without them.
- Read Reporting Configuration (ZCL 2.5.9): each record gets UNSUPPORTED_ATTRIBUTE, UNREPORTABLE_ATTRIBUTE (also for direction 0x01), NOT_FOUND without a configuration, or SUCCESS with the data type, the intervals and, for an analog type, the reportable change. The records that do not fit are left out.
- Report Attributes (ZCL 2.5.11):
  - A discrete attribute is reported at any change, an analog one when it differs from the value of the last report by at least the reportable change; a reportable change of 0 reports any change.
  - A change within the minimum interval after the last report waits for its end. A periodic report follows the maximum interval after the last report; a maximum interval of 0 sends no periodic reports.
  - Configure Reporting sends no report. It takes the current value as the value of the last report and starts the maximum interval.
  - The attributes of one endpoint and cluster that are due at the same time go in one frame.
  - The frame goes from the endpoint to the bound destinations with the function of Clients, direction to the client. Without a binding nothing is sent.
  - A report that APSDE-DATA refuses with `-ENOTCONN` or `-EBUSY`, which happens off a network and during a rejoin, stays due and is tried again after 5 s. One refused with `-ENOBUFS`, while the APS transmissions (`CONFIG_ZZB_APS_TX_COUNT`, default 4) or the buffers are in use, is tried again after 1 s: an application that sets six measurements of a Smart Plug at once makes six frames with an APS ACK request. After another error, such as no binding, it counts as sent. The retry wakes the CPU of a node off a network every 5 s and does not turn the radio on, so it stays the same on a sleepy node.
- The change is checked when a value is stored, also by `zzb_zcl_attr_set()`. Loading the records does not check it.
- `zzb_zcl_start()` takes the values of the last report after all records are loaded and before the `start` hooks. A change of OnOff by StartUpOnOff is therefore reported once the node is back on its network: ZHA 2.3.1 does not read OnOff again after a device reboots.

### Basic

- ClusterRevision 3, ZCLVersion 8. The attributes are those that the DTL requires for the On/Off Light: ApplicationVersion, StackVersion, HWVersion, ManufacturerName, ModelIdentifier, DateCode, PowerSource, GenericDeviceClass, GenericDeviceType, ProductCode, ProductURL and SWBuildID, all read-only and set by the application. GenericDeviceClass and GenericDeviceType default to 0xff in ZCL, while a zeroed struct means lighting and incandescent, so the application sets them.
- Reset to Factory Defaults (optional) is not implemented.

### Identify

- ClusterRevision 2. The Identify command, a write and `zzb_zcl_attr_set()` set IdentifyTime. A timer of the node decrements every IdentifyTime above 0 once a second, with `zcl_cb`, and is not armed while all are 0. The tick is shared, so the first decrement of an instance that started later can come early.
- Identify Query: with IdentifyTime above 0, Identify Query Response with the remaining time goes as unicast to the source of the query, also of a broadcast query (ZCL 3.5.2.3.2.1).
- Trigger Effect goes to `zcl_effect_cb` and changes no attribute.
- `zzb_zcl_identify(node, ep, time)` sets IdentifyTime of the server on ep with `zcl_cb` for finding & binding, or returns `-ENOENT`. The server calls `zzb_bdb_identify_end(node, ep)` whenever its IdentifyTime becomes 0.
- `ZZB_ZCL_IDENTIFY_CLIENT()` is a client instance whose definition has ClusterRevision 2 and passes Identify Query Response to `zzb_bdb_identify_query_rsp()` (section 13).

### On/Off

- ClusterRevision 2. Attributes: OnOff (read, report, non-volatile), GlobalSceneControl, OnTime, OffWaitTime and StartUpOnOff (writable, non-volatile; 0x00, 0x01, 0x02 and 0xff, other values give INVALID_VALUE). The value struct also holds `start_up_default`, the factory value of StartUpOnOff (MS in ZCL), which the application sets and the stack does not change, and internal fields.
- Commands (ZCL 3.8.2.3):
  - Off: OnOff 0, OnTime 0.
  - On: OnOff 1, GlobalSceneControl TRUE, and OffWaitTime 0 when OnTime is 0.
  - Toggle: as On when off; OnOff 0 and OnTime 0 when on.
  - Off With Effect: with GlobalSceneControl TRUE, the global scene is stored, GlobalSceneControl becomes FALSE, OnOff 0 and OnTime 0; otherwise OnOff 0.
  - On With Recall Global Scene: discarded with GlobalSceneControl TRUE. Otherwise the global scene is recalled, GlobalSceneControl becomes TRUE, and OffWaitTime 0 when OnTime is 0.
  - The global scene is the scene table entry with group 0 and scene 0 (ZCL 3.8.2.2.2) when the endpoint has a Scenes server: Off With Effect stores it as Store Scene does and On With Recall Global Scene recalls it as Recall Scene does; without the entry OnOff does not change. On an endpoint without a Scenes server the global scene is OnOff in a RAM field of the value struct.
- OnOff is the scene extension field set. The `recall` hook writes OnOff at once and sets GlobalSceneControl TRUE when OnOff is TRUE (ZCL 3.8.2.2.2), without the fades of Level Control.
- With a Level Control server on the endpoint, Off, On and Toggle also change the level (Level Control).
  - On With Timed Off (ZCL 3.8.2.3.6.4): discarded with Accept Only When On while off. While off with OffWaitTime above 0, OffWaitTime takes the minimum of itself and the field. Otherwise OnTime takes the maximum of itself and the field, OffWaitTime the field, OnOff 1 and GlobalSceneControl TRUE. When both are below 0xffff, the update starts.
- The update runs only after On With Timed Off, as ZCL 3.8.2.3.6.4 describes it; a write of OnTime does not start it. Every 0.1 s it decrements OnTime while on, and at 0 sets OffWaitTime 0 and OnOff 0; while off it decrements OffWaitTime. It ends when both are 0. One timer of the node serves all instances and is not armed without an update.
- StartUpOnOff is applied once in `zzb_node_init()` after the records are loaded, without `zcl_cb`: 0x00 and 0x01 set OnOff, 0x02 inverts it and saves the result, 0xff and the reserved values keep the loaded value. Without records StartUpOnOff is `start_up_default` and OnOff 0.

### Groups

- ClusterRevision 3 (ZCL 3.6). `ZZB_ZCL_GROUPS_SERVER()` has no value struct: NameSupport is the constant 0, and group names are not stored. ZCL 3.6.2.1 requires an Identify server on the endpoint.
- Group IDs outside 0x0001 to 0xfff7 give INVALID_VALUE. The commands use the APS group table (section 11); a full table gives INSUFFICIENT_SPACE.
- Add Group reads only the Group ID; View Group answers with an empty name. Get Group Membership answers with the capacity, the number of free entries up to 0xfd, and the groups of the endpoint, all of them for a group count of 0, otherwise those in the request; without a match it answers only a unicast, with count 0 (ZCL 3.6.2.3.4). Remove Group also removes the scenes of the group on the endpoint, Remove All Groups the scenes of every group except 0. Add Group If Identifying adds the group only while the Identify server of the endpoint identifies.
- Add Group, View Group and Remove Group answer only a unicast (ZCL 3.6.2.3.1); Remove All Groups and Add Group If Identifying have no response of their own and follow the Default Response rule.

### Scenes

- ClusterRevision 3 (ZCL 3.7). Attributes: SceneCount, CurrentScene, CurrentGroup and SceneValid in `struct zzb_zcl_scenes`, NameSupport the constant 0; scene names are not stored, and LastConfiguredBy (optional) is not implemented. ZCL 3.7.2.1 requires a Groups server on the endpoint.
- The scene table of the node has `CONFIG_ZZB_ZCL_SCENE_COUNT` entries (default 16, ZCL 3.7.2.3.2) shared by the endpoints: endpoint, group, scene, transition time, TransitionTime100ms and up to `CONFIG_ZZB_ZCL_SCENE_DATA_LEN` (default 24, the sets of On/Off, Level Control and Color Control) bytes of extension field sets.
- The extension field set of a cluster is the list `scene_attrs` of its definition: attribute IDs and types in the order of the set, which for Color Control (ZCL 5.2.2.5) is not the order of the IDs. An attribute of the list that the cluster does not have is stored as 0 and ignored at a recall (ZCL 5.2.2.5).
- The entry keeps the sets in their ZCL encoding, only for the server clusters of the endpoint with a set, in the order of the endpoint, each cut to the attributes the cluster has. A set missing from Add Scene stays missing, and Recall Scene leaves that cluster unchanged (ZCL 3.7.2.4.7).
- Every command first checks the group: outside 0x0000 to 0xfff7 INVALID_VALUE, a group other than 0 of which the endpoint is not a member INVALID_FIELD (ZCL 3.7.2.4.1). A full table gives INSUFFICIENT_SPACE only for a new entry; an existing entry is overwritten, as Copy Scene does.
- Commands: Add Scene, View Scene, Remove Scene, Remove All Scenes, Store Scene, Recall Scene, Get Scene Membership, and Enhanced Add Scene, Enhanced View Scene and Copy Scene, which the DTL requires for the On/Off Light. Copy Scene with a missing source gives INVALID_FIELD (ZCL Table 3-44).
- Recall Scene decodes each set by the list and passes the values to the `recall` hook of the cluster with the transition time in tenths of a second: the Transition Time field of the command when it is present and not 0xffff (ZCL 3.7.2.4.7), otherwise the transition time and TransitionTime100ms of the entry. A set without its trailing attributes passes fewer values. A list has at most `ZZB_ZCL_SCENE_ATTRS_MAX` (8) attributes, and a cluster without the hook keeps its state.
- The responses answer only a unicast, except Get Scene Membership, which also answers a group or broadcast request when the table has a scene of the group (ZCL 3.7.2.4.8).
- SceneCount is the number of entries of the endpoint. A change of an attribute of a scene extension field set sets SceneValid FALSE. The steps of a transition do not change SceneValid: a transition that a command starts sets it FALSE once at its start, one that a recall starts leaves it TRUE. Recall Scene writes the attributes, then sets CurrentScene, CurrentGroup and SceneValid TRUE; Store Scene sets the same three. Changes caused by commands and timers come with `zcl_cb`; a change of SceneValid caused by `zzb_zcl_attr_set()` does not, and the `zcl_cb` of SceneValid comes before that of the attribute that changed.

### Transitions

- Level Control and Color Control move their attributes over a transition time in tenths of a second. `transition.c` keeps one transition in `struct zzb_zcl_transition`, an internal field of the value struct: the start time, the duration, the start and end values of up to two attributes and the values written last. A step computes its value from `k_uptime_get()`, so late ticks do not accumulate.
- Each of the two clusters has one `k_work_delayable` per node, which steps all its transitions every 100 ms, the unit of ZCL transition times, and is armed only while a transition runs, as the timer of `report.c`.
- A step writes through `zzb_zcl_update()`: the `changed` hook, the reporting check, `zcl_cb` and RemainingTime, without saving the record and without SceneValid. The end of a transition, Stop and a move that reaches its limit save the record with `zzb_zcl_save()`, so a transition of 2 s writes one record instead of 20.
- A move at a rate is a transition to the limit with the duration distance / rate. A transition time of 0 writes the value at once. A new command of the cluster replaces the running transition. When the value of the attribute differs from the value the transition wrote last, because the application has set it with `zzb_zcl_attr_set()`, the transition ends without a write.

### Level Control

- `ZZB_ZCL_LEVEL_SERVER(&v)` is Level Control for Lighting (ZCL 3.19), ClusterRevision 2 (ZCL 3.19.1.1). Attributes: CurrentLevel (report, scene, non-volatile), RemainingTime, Options, OnOffTransitionTime, OnLevel, OnTransitionTime, OffTransitionTime and StartUpCurrentLevel (writable, non-volatile). The DTL requires CurrentLevel, RemainingTime, Options and StartUpCurrentLevel; ZHA 2.3.1 reads the transition times and OnLevel and creates entities for them. MinLevel, MaxLevel, DefaultMoveRate and the frequency attributes are not implemented. The level is 1 to 254 (ZCL 3.19.2.2.1).
- The value struct holds `start_up_default`, the factory value of StartUpCurrentLevel (MS in ZCL), which the application sets. Without records CurrentLevel is 254.
- Commands: Move to Level, Move, Step, Stop and their forms with On/Off (ZCL 3.10.2.3). The OptionsMask and OptionsOverride fields are optional, and ZHA does not send them; the options of a command are (Options & ~mask) | (override & mask) (ZCL 3.10.2.3.1.2). With an On/Off server on the endpoint, OnOff FALSE and ExecuteIfOff 0, a command without On/Off is discarded with SUCCESS (ZCL 3.10.2.2.8.1).
- Move to Level limits the level to 1 to 254, so level 0, which ZHA sends to turn off with a transition, moves to 1. Transition time 0xffff takes OnOffTransitionTime. Move with rate 0 gives INVALID_FIELD; rate 0xff moves to the limit at once, since DefaultMoveRate is not implemented. A reserved Move or Step mode gives INVALID_FIELD. Step at a limit shortens its transition time in proportion; transition time 0xffff steps at once. Stop leaves the level and sets RemainingTime 0.
- A command with On/Off whose target is above 1 sets OnOff TRUE before the transition. Reaching 1 sets OnOff FALSE at the end of the transition; ZCL 3.10.2.3.6 does not say when, and setting it at the start would turn the light off before it dims. The changes of OnOff follow the On and Off rules of the On/Off cluster for GlobalSceneControl, OnTime and OffWaitTime (ZCL 3.10.2.1.3, 3.8.2.1.1) through `zzb_zcl_on_off_switch()` of `on_off.c`.
- The On/Off server calls `zzb_zcl_level_on_off()` for Off, On and Toggle when the endpoint has a Level Control server (ZCL Table 3-55):
  - On while off: the level is stored, CurrentLevel becomes 1 and OnOff TRUE, and the level moves to OnLevel, or to the stored level when OnLevel is 0xff. With transition time 0 the target is written at once, without the step to 1.
  - Off while on: the level is stored, OnTime becomes 0 at once, so that its timer does not turn the light off during the fade, and the level moves to 1. At the end OnOff becomes FALSE and, with OnLevel 0xff, CurrentLevel the stored level. With transition time 0 and OnLevel 0xff only OnOff changes.
  - On or Toggle during this fade moves the level back up to the same stored level (ZCL 3.10.2.1.1), and Off is ignored. A Level Control command cancels the change of OnOff at the end.
  - Off or Toggle during the fade of On, also one that reverses the fade of Off, keeps the stored level and moves down from the current level (ZCL 3.10.2.1.1); a Level Control command ends the fade of On, so that a later Off stores the level again.
  - On while on and Off while off do not change the level; Table 3-55 does not cover these cases.
  - The transition time is OnTransitionTime or OffTransitionTime when it is not 0xffff, otherwise OnOffTransitionTime. ZCL defines the first two for the whole range of levels, so the stack scales them by the distance.
  - OnOff becomes FALSE at the end of the fade, not when Off is received (ZCL 3.8.2.3.1): the application turns its output off by OnOff, and the fade would not be visible.
- Off With Effect, On With Recall Global Scene, On With Timed Off and the OnTime timer do not change the level; the application plays the effect of Off With Effect.
- The `start` hook applies StartUpCurrentLevel: 0x00 sets 1, 0xff keeps the loaded CurrentLevel, another value sets itself limited to 1 to 254.
- The reset of the node sets CurrentLevel 254, Options 0, OnOffTransitionTime 0, OnLevel 0xff, OnTransitionTime and OffTransitionTime 0xffff and StartUpCurrentLevel `start_up_default`, and stops the transition.

### Color Control

- `ZZB_ZCL_COLOR_SERVER(&v)`, ClusterRevision 3, implements the XY (bit 3 of ColorCapabilities) and color temperature (bit 4) capabilities, which the DTL requires for the Color Dimmable Light (at least XY), the Color Temperature Light (0x0010) and the Extended Color Light (0x0018). Hue and saturation, enhanced hue and color loop are not implemented; ZHA 2.3.1 sends only Move to Color, Move to Color Temperature and Color Loop Set.
- The application sets ColorCapabilities (map16), ColorTempPhysicalMinMireds, ColorTempPhysicalMaxMireds, CoupleColorTempToLevelMinMireds, NumberOfPrimaries and Primary1 to Primary6 X, Y and Intensity; they are read-only. The stack keeps CurrentX, CurrentY and ColorTemperatureMireds (report, scene, non-volatile), RemainingTime, ColorMode and EnhancedColorMode (non-volatile), Options and StartUpColorTemperatureMireds (writable, non-volatile). `start_up_default` is the factory value of StartUpColorTemperatureMireds. The WhitePoint and ColorPoint attributes are not implemented.
- Commands: Move to Color, Move Color and Step Color with bit 3; Move to Color Temperature, Move Color Temperature and Step Color Temperature with bit 4; Stop Move Step with either. Without the bit, and for the commands of the other capabilities, UNSUP_COMMAND (ZCL 5.2.2.2.1.18). Options and its ExecuteIfOff bit act as in Level Control, for every command (ZCL 5.2.2.2.1.10). A command sets ColorMode and EnhancedColorMode: 1 for XY, 2 for color temperature.
- Move Color with both rates 0 stops; otherwise it moves until X or Y reaches 0 or 0xfeff. The color temperature commands limit their target to the physical range and succeed (ZCL 5.2.2.3.1); a minimum or maximum of 0 in Move and Step Color Temperature means the physical limit.
- With bit 3, CurrentX and CurrentY follow ColorTemperatureMireds in color temperature mode, by the approximation of Kim et al. (2002) for 1667 K to 25000 K. The `changed` hook of ColorTemperatureMireds writes them through `zzb_zcl_update()`, so they are not saved, and the `start` hook computes them from the loaded ColorTemperatureMireds without `zcl_cb`. A change to XY mode starts from the current X and Y, and Store Scene stores the color temperature as XY (ZCL 5.2.2.5). A change from XY mode to color temperature starts from the temperature of the McCamy formula for the current x and y, within the physical range. The conversions use `float`; ESP32-C6 and ESP32-H2 have no FPU, so they run in software, at most once per step.
- The scene extension field set is the list of ZCL 5.2.2.5: CurrentX, CurrentY, EnhancedCurrentHue, CurrentSaturation, ColorLoopActive, ColorLoopDirection, ColorLoopTime, ColorTemperatureMireds; the four absent attributes are stored as 0. A recall moves to X and Y in XY mode with bit 3, otherwise to ColorTemperatureMireds.
- With bit 1 (CoupleColorTempToLevel) of the Options of Level Control, color temperature mode and bit 4, every change of CurrentLevel sets ColorTemperatureMireds through `zzb_zcl_update()` (ZCL 5.2.2.1.1): 254 gives CoupleColorTempToLevelMinMireds and 1 ColorTempPhysicalMaxMireds, linear between.
- The `start` hook applies StartUpColorTemperatureMireds with bit 4 (ZCL Table 5.9): 0xffff keeps the loaded value, another value sets itself within the physical range, and ColorMode and EnhancedColorMode become 2. Without records the mode is color temperature with bit 4, otherwise XY, CurrentX 0x616b, CurrentY 0x607d and ColorTemperatureMireds 0x00fa within the physical range.
- The reset of the node sets these defaults and stops the transition.

### Clients

- `ZZB_ZCL_ON_OFF_CLIENT()`, `ZZB_ZCL_LEVEL_CLIENT()`, `ZZB_ZCL_COLOR_CLIENT()`, `ZZB_ZCL_GROUPS_CLIENT()`, `ZZB_ZCL_SCENES_CLIENT()`, `ZZB_ZCL_ILLUMINANCE_CLIENT()`, `ZZB_ZCL_ILLUMINANCE_LEVEL_CLIENT()`, `ZZB_ZCL_OCCUPANCY_CLIENT()`, `ZZB_ZCL_ALARMS_CLIENT()`, `ZZB_ZCL_WINDOW_COVERING_CLIENT()`, `ZZB_ZCL_THERMOSTAT_CLIENT()`, `ZZB_ZCL_FAN_CLIENT()`, `ZZB_ZCL_PUMP_CLIENT()`, `ZZB_ZCL_TEMPERATURE_CLIENT()`, `ZZB_ZCL_PRESSURE_CLIENT()`, `ZZB_ZCL_FLOW_CLIENT()`, `ZZB_ZCL_HUMIDITY_CLIENT()`, `ZZB_ZCL_IAS_WD_CLIENT()`, `ZZB_ZCL_DOOR_LOCK_CLIENT()`, `ZZB_ZCL_METERING_CLIENT()`, `ZZB_ZCL_METER_ID_CLIENT()`, `ZZB_ZCL_ELECTRICAL_MEASUREMENT_CLIENT()` and `ZZB_ZCL_APPLIANCE_ID_CLIENT()` are client instances with ClusterRevision, which the DTL requires on every cluster; each cluster file defines its client next to its server. They accept any cluster-specific command with SUCCESS, such as the responses to a scene controller and Alarm. Every client instance accepts a Report Attributes with SUCCESS, and the values do not reach the application. Without this a client of Occupancy Sensing would answer every bound report with an error, since error statuses ignore Disable Default Response. `ZZB_ZCL_IDENTIFY_CLIENT()` keeps its definition (Identify), `ZZB_ZCL_TIME_CLIENT()` sets the clock of the node (Time), `ZZB_ZCL_OTA_CLIENT(&v)` downloads firmware images (OTA Upgrade), and `ZZB_ZCL_IAS_ZONE_CLIENT()`, `ZZB_ZCL_IAS_ACE_CLIENT()`, `ZZB_ZCL_POWER_PROFILE_CLIENT()`, `ZZB_ZCL_APPLIANCE_CONTROL_CLIENT()`, `ZZB_ZCL_APPLIANCE_EVENTS_CLIENT()`, `ZZB_ZCL_APPLIANCE_STATS_CLIENT()` and `ZZB_ZCL_POLL_CONTROL_CLIENT()` pass their commands to `zcl_cmd_cb` (IAS Zone, IAS ACE, Power Profile, EN50523 appliance clusters, Poll Control).
- One internal function sends a frame to the bound destinations: APSDE-DATA with destination address mode 0x00, the profile of the endpoint, Disable Default Response 1, an APS ACK request for IEEE destinations, the TSN of the node and ASDU handle 0xfd. Reports, Alarm and `zzb_zcl_send_bound()` use it.
- `zzb_zcl_send_bound(node, ep, cluster, cmd, payload, len)` sends a cluster-specific command of the client instance on ep to the bound destinations, or of the server instance, with the direction to the client, when ep has no client instance of the cluster (IAS ACE). It runs on the stack thread through `zzb_call_sync()` and returns `-ENOENT` without an instance of the cluster or without a binding, and the other errors of APSDE-DATA.request, such as `-ENOBUFS` and `-ENOTCONN`. No confirm follows. A sent command calls `zzb_nwk_poll_fast()`, so that a sleepy node fetches a response, such as Lock Door Response; reports, Alarm and notifications do not. The cluster headers define the command identifiers; the application builds the payload.

### Alarms

- `ZZB_ZCL_ALARMS_SERVER(&v)`, ClusterRevision 1, has AlarmCount, the number of entries of its endpoint in the alarm table.
- The alarm table of the node has `CONFIG_ZZB_ZCL_ALARM_COUNT` entries (default 8), shared by the endpoints: sequence number, endpoint, alarm code, cluster and time stamp. The time stamp is the UTC time of the clock of the node when the alarm occurs, or 0xffffffff while the node has no time (ZCL Table 3-66, Time). The earliest entry has the lowest sequence number, and a full table replaces it (ZCL 3.11.2.3). Masked alarms enter the table too (ZCL 3.11.1). The table is persistent data (ZCL 2.3.5): each entry has a record (section 15), and the sequence number spares the other records a rewrite when an entry is removed.
- Commands: Reset Alarm, Reset All Alarms, Get Alarm, which answers with the earliest entry of the endpoint and removes it, or with NOT_FOUND, and Reset Alarm Log. Get Alarm Response also answers a broadcast, like Read Attributes.
- The cluster that detects a condition keeps its state in internal fields of its value struct. The `start` hook checks the conditions after the records are loaded, so a condition that lasts across a reboot raises its alarm again, after its dwell. An alarm occurs once when its condition starts and resets itself when the condition ends; ZCL 3.11.1 leaves the choice between automatic and explicit reset to the implementation. Reset Alarm calls the `alarm_reset(node, ep, values, code)` hook of the server of its cluster on the endpoint, Reset All Alarms the hook of every server on the endpoint with code 0xff. The hook raises the alarm again while its condition lasts (ZCL 3.11.2.4.1).
- An alarm enters the table when the endpoint has an Alarms server (ZCL 3.4.2.1, 5.3.2.1). When the alarm mask of its cluster enables it, it also goes as Alarm (alarm code, cluster) from the endpoint to the bound destinations of the Alarms cluster. Without a binding or off a network it is not sent again; Get Alarm reads it from the table.
- `zzb_zcl_alarm(node, ep, cluster, code)` raises an alarm of the application on the stack thread through `zzb_call_sync()` and always sends Alarm, or returns `-ENOENT` without an Alarms server on ep. ZCL gives no condition for the mains lost alarm of Power Configuration (code 0x3a, bit 2 of MainsAlarmMask, bit 30 of BatteryAlarmState), so the application raises it and sets the bit with `zzb_zcl_attr_set()`; the stack changes only bits 0 to 3.
- ZHA 2.3.1 does not bind or read the Alarms cluster; zigpy logs a received Alarm.

### Power Configuration

- `ZZB_ZCL_POWER_CONFIG_SERVER(&v)`, ClusterRevision 2, has the Mains Information, Mains Settings, Battery Information and Battery Settings sets of the first battery source (ZCL 3.3.2.2); sources 2 and 3 are not implemented. The application sets MainsVoltage, MainsFrequency, BatteryVoltage, BatteryPercentageRemaining, BatteryManufacturer, BatterySize, BatteryAHrRating, BatteryQuantity and BatteryRatedVoltage. MainsAlarmMask, the mains voltage thresholds, MainsVoltageDwellTripPoint, BatteryAlarmMask and the battery voltage and percentage thresholds are writable and non-volatile. BatteryVoltage, BatteryPercentageRemaining and BatteryAlarmState are reportable.
  - BatteryVoltage is reportable although ZCL marks it R: ZHA 2.3.1 configures its report.
  - The five attributes that describe the battery are read-only although ZCL makes them writable: they describe the battery the device is built with, and writable attributes would need their factory values passed separately.
- Mains voltage alarms (ZCL 3.3.2.2.2): MainsVoltage below MainsVoltageMinThreshold, or above MainsVoltageMaxThreshold, for MainsVoltageDwellTripPoint seconds gives alarm 0x00, or 0x01, with bit 0, or 1, of MainsAlarmMask. A threshold of 0xffff and a MainsVoltage of 0xffff give no alarm; the dwell trip point does not disable it (CCB 2899). One timer of the node is armed for the earliest deadline.
- Battery alarms (ZCL 3.3.2.2.4): BatteryVoltage below BatteryVoltageMinThreshold, or BatteryPercentageRemaining below BatteryPercentageMinThreshold, sets bit 0 of BatteryAlarmState and gives alarm 0x10; thresholds 1 to 3 of voltage or percentage set bits 1 to 3 and give alarms 0x11 to 0x13, with the bits of BatteryAlarmMask. There is no dwell. A threshold or a value of 0xff gives no condition. A bit is cleared when its condition ends (ZCL 3.3.2.2.4.11). The percentage thresholds are in percent and BatteryPercentageRemaining in half percent, so half the value is compared; ZCL does not state the unit of the thresholds. The application applies the hysteresis of ZCL 3.3.2.2.3 to the values it sets.

### Device Temperature Configuration

- `ZZB_ZCL_DEVICE_TEMP_SERVER(&v)`, ClusterRevision 1. The application sets CurrentTemperature (int16, degrees Celsius) and OverTempTotalDwell (read-only, non-volatile). CurrentTemperature is reportable although ZCL does not mark it: ZHA 2.3.1 configures its report, and divides the value by 100 while ZCL and zigpy define whole degrees. The stack keeps MinTempExperienced and MaxTempExperienced (non-volatile) from CurrentTemperature at the start and at its changes; they are 0x8000 until the first known temperature, and the reset of the node sets them to 0x8000. DeviceTempAlarmMask, LowTempThreshold and HighTempThreshold (int16), LowTempDwellTripPoint and HighTempDwellTripPoint (uint24) are writable and non-volatile.
- CurrentTemperature below LowTempThreshold for LowTempDwellTripPoint seconds gives alarm 0x00, above HighTempThreshold for HighTempDwellTripPoint seconds alarm 0x01, with bit 0, or 1, of DeviceTempAlarmMask. A threshold of 0x8000 or a dwell trip point of 0xffffff disables the alarm (ZCL 3.4.2.2.2), and a CurrentTemperature of 0x8000 gives none. One timer of the node is armed for the earliest deadline.

### Ballast Configuration

- `ZZB_ZCL_BALLAST_SERVER(&v)`, ClusterRevision 3. The application sets PhysicalMinLevel, PhysicalMaxLevel, BallastStatus and LampQuantity. MinLevel and MaxLevel are writable and non-volatile, with the physical levels as factory values; a write that breaks PhysicalMinLevel ≤ MinLevel ≤ MaxLevel ≤ PhysicalMaxLevel gives INVALID_VALUE. IntrinsicBallastFactor (default 0xff, unknown, since ZCL gives none), BallastFactorAdjustment (0x64 and above, or 0xff), LampRatedHours, LampBurnHours, LampAlarmMode and LampBurnHoursTripPoint are writable and non-volatile; the application counts LampBurnHours with `zzb_zcl_attr_set()`. PowerOnLevel and PowerOnFadeTime (deprecated), LampType and LampManufacturer are not implemented. The stack does not link Ballast Configuration to Level Control; the application maps CurrentLevel to its output with MinLevel and MaxLevel (ZCL 5.3.4).
- LampBurnHours at or above LampBurnHoursTripPoint gives alarm 0x01 with bit 0 of LampAlarmMode; a trip point of 0xffffff disables it (ZCL 5.3.2.2.4.6), and LampBurnHours of 0xffffff, unknown (ZCL 5.3.2.2.4.4), gives none.

### Measurement and sensing

- `ZZB_ZCL_ILLUMINANCE_SERVER(&v)`, Illuminance Measurement, ClusterRevision 2: MeasuredValue (reportable), MinMeasuredValue, MaxMeasuredValue, Tolerance and LightSensorType, set by the application.
- `ZZB_ZCL_ILLUMINANCE_LEVEL_SERVER(&v)`, Illuminance Level Sensing, ClusterRevision 1: LevelStatus (reportable) and LightSensorType, set by the application, and IlluminanceTargetLevel, writable and non-volatile, default 0xffff. The application computes LevelStatus with its dead band (ZCL 4.3.2.4.1).
- `ZZB_ZCL_OCCUPANCY_SERVER(&v)`, Occupancy Sensing, ClusterRevision 2: Occupancy (reportable), OccupancySensorType and OccupancySensorTypeBitmap, set by the application, and the delays and thresholds of the PIR, ultrasonic and physical contact sets, writable and non-volatile, with the ZCL defaults; a threshold outside 0x01 to 0xfe gives INVALID_VALUE. The application detects occupancy and runs the delays.
- `ZZB_ZCL_TEMPERATURE_SERVER(&v)`, Temperature Measurement, ClusterRevision 3, `ZZB_ZCL_FLOW_SERVER(&v)`, Flow Measurement, ClusterRevision 2, and `ZZB_ZCL_HUMIDITY_SERVER(&v)`, Relative Humidity Measurement, ClusterRevision 2: MeasuredValue (reportable), MinMeasuredValue, MaxMeasuredValue and Tolerance, set by the application.
- `ZZB_ZCL_PRESSURE_SERVER(&v)`, Pressure Measurement, ClusterRevision 2: the same attributes, and ScaledValue (reportable), MinScaledValue, MaxScaledValue, ScaledTolerance and Scale (int8) of the extended set, set by the application; ZCL 4.5.2.2.2 requires all of them or none.
- ZHA 2.3.1 configures the report of MeasuredValue: temperature 30/900/50, pressure 30/900/1, shown as hPa, flow 30/900/1, relative humidity 30/900/100.

### Window Covering

- `ZZB_ZCL_WINDOW_COVERING_SERVER(&v)`, ClusterRevision 3 (ZCL 7.4). The application sets WindowCoveringType, PhysicalClosedLimitLift and PhysicalClosedLimitTilt, CurrentPositionLift and CurrentPositionTilt, NumberOfActuationsLift and NumberOfActuationsTilt, Config/Status, CurrentPositionLiftPercentage and CurrentPositionTiltPercentage (reportable, scene; 0xff unknown) and the four installed limits; they are read-only. Mode is writable and non-volatile; bits 4 to 7 give INVALID_VALUE. VelocityLift, AccelerationTimeLift, DecelerationTimeLift and the intermediate setpoints are not implemented.
- Mode defaults to 0x00, not 0x04 of ZCL Table 7-43: bit 2 is the maintenance mode, in which the motor does not move on network commands.
- WindowCoveringType gives the axes, as in ZHA 2.3.1: tilt with 0x06, 0x07 and 0x08, lift with every type except 0x06 and 0x07.
- Up/Open, Down/Close and Stop go to `zcl_cmd_cb` at once. Go To Lift Percentage and Go To Tilt Percentage give UNSUP_COMMAND without the axis and INVALID_VALUE above 100. Go To Lift Value and Go To Tilt Value give UNSUP_COMMAND without the axis and INVALID_VALUE outside the installed open and closed limits of the axis, taken in either order, since ZCL 7.4.2.2.4 states the bounds the other way round. A command that passes goes to `zcl_cmd_cb` as received.
- The stack does not interpret Mode and Config/Status: the motor direction, calibration, maintenance and the open-loop rule of ZCL 7.4.2.2.5 (0 % closes) are left to the application.
- The scene extension field set is CurrentPositionLiftPercentage, CurrentPositionTiltPercentage. A recall passes Go To Lift Percentage and Go To Tilt Percentage to `zcl_cmd_cb`, for each axis of the type with a value of at most 100, in a `struct zzb_zcl_cmd` that the stack builds with `unicast` false and source address, source endpoint and TSN 0; the status it returns is ignored. The transition time is ignored: the motor moves at its own speed. The first position that the application sets after the recall makes SceneValid FALSE.
- The reset of the node sets Mode 0.
- ZHA 2.3.1 configures the reports of both percentages 0/900/1, shows 100 minus CurrentPositionLiftPercentage as the position and sends only Up/Open, Down/Close, Stop, Go To Lift Percentage and Go To Tilt Percentage. Its inversion switch writes bit 0 of Mode and shows bit 2 of Config/Status, so the application that reverses the motor sets that bit.

### Thermostat

- `ZZB_ZCL_THERMOSTAT_SERVER(&v)`, ClusterRevision 3 (ZCL 6.3). Temperatures are int16 in 0.01 °C.
- The application sets, read-only: LocalTemperature (reportable), OutdoorTemperature, Occupancy, AbsMinHeatSetpointLimit, AbsMaxHeatSetpointLimit, AbsMinCoolSetpointLimit, AbsMaxCoolSetpointLimit, PICoolingDemand and PIHeatingDemand (reportable), MinSetpointDeadBand (int8, 0.1 °C), ControlSequenceOfOperation, AlarmMask, ThermostatRunningMode, ThermostatRunningState and StartOfWeek. A zeroed struct has absolute limits of 0 and Occupancy 0 (unoccupied); the ZCL defaults are 700, 3000, 1600 and 3200 and Occupancy 1. `system_mode_default` is the factory value of SystemMode; 0 is Off.
  - ControlSequenceOfOperation is read-only although ZCL makes it writable: it describes the equipment the thermostat controls, and a writable attribute would need its factory value passed separately. MinSetpointDeadBand is read-only, which ZCL allows (R*W).
  - Occupancy, ThermostatRunningMode and ThermostatRunningState are reportable although ZCL does not mark them: ZHA 2.3.1 configures their reports.
- The stack keeps, writable and non-volatile: LocalTemperatureCalibration (int8, 0.1 °C), OccupiedCoolingSetpoint, OccupiedHeatingSetpoint, UnoccupiedCoolingSetpoint and UnoccupiedHeatingSetpoint (reportable, the occupied ones in the scene), MinHeatSetpointLimit, MaxHeatSetpointLimit, MinCoolSetpointLimit and MaxCoolSetpointLimit, SystemMode (reportable, scene) and ThermostatProgrammingOperationMode (reportable). NumberOfDailyTransitions is the constant `CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS` and NumberOfWeeklyTransitions 7 times it. The application applies LocalTemperatureCalibration to its measurement and sets LocalTemperature.
- Before the records are loaded the limits take the absolute limits, the cooling setpoints 2600 and the heating setpoints 2000 within them, SystemMode `system_mode_default`, and the weekly schedule has no transitions. The reset of the node sets the same values and LocalTemperatureCalibration and ThermostatProgrammingOperationMode 0, and clears the weekly schedule and deletes its records.
- Writes are checked against the values before the command (Data model):
  - A setpoint outside its minimum and maximum limit gives INVALID_VALUE.
  - With ControlSequenceOfOperation 4 or 5, a heating setpoint above the cooling setpoint of its pair (occupied or unoccupied) minus 10 × MinSetpointDeadBand moves that cooling setpoint up to the heating setpoint plus the dead band, and a cooling setpoint below the heating setpoint of its pair plus the dead band moves that heating setpoint down; the moved setpoint comes with `zcl_cb`, also after `zzb_zcl_attr_set()`. A setpoint that would move the other one beyond its limit gives INVALID_VALUE. ZCL 6.3.2.2.2.2 asks for INVALID_VALUE in every case, but ZHA 2.3.1 writes only the setpoint of its heat or cool mode, and a user could not raise the heating setpoint above the cooling setpoint minus the dead band without switching to heat_cool first.
  - The dead band does not apply to the other sequences, which do not both heat and cool: a thermostat that only heats would otherwise not take a heating setpoint above 23.5 °C because of the cooling setpoint it does not use.
  - A limit gives INVALID_VALUE unless AbsMin ≤ Min ≤ Max ≤ AbsMax holds for its kind and the occupied and unoccupied setpoints of its kind stay within it; the setpoints are not moved.
  - SystemMode 0x02 and the values above 0x09 are reserved. With ControlSequenceOfOperation 0 or 1, Heat (0x04) and Emergency heating (0x05), and with 2 or 3, Cool (0x03) and Precooling (0x06), give INVALID_VALUE (ZCL Table 6-15).
  - LocalTemperatureCalibration outside -25 to 25 is limited to the nearer end with SUCCESS (ZCL 6.3.2.2.2.1, CCB 2477); the `changed` hook stores the limited value.
- Setpoint Raise/Lower (ZCL 6.3.2.3.1): a Mode above 2 gives INVALID_FIELD. The occupied setpoints change, or the unoccupied ones while bit 0 of Occupancy is 0. A setpoint moves by 10 × Amount within its limits and moves the other setpoint of its pair by the dead band as a write does; with ControlSequenceOfOperation 4 or 5 a heating setpoint stays at or below the maximum cooling limit minus the dead band and a cooling setpoint at or above the minimum heating limit plus it. Mode 2 moves the cooling setpoint first in a raise and the heating setpoint first in a lower, so that the first one does not move the second by the dead band. The command gives SUCCESS.
- A recalled scene writes OccupiedCoolingSetpoint, OccupiedHeatingSetpoint and SystemMode at once, without the checks; the transition time is ignored. The dead band moves the heating setpoint after the cooling setpoint is written, so a scene whose setpoints keep the dead band is restored as stored.
- The application raises the alarms 0x00 to 0x02 with `zzb_zcl_alarm()` when their bits of AlarmMask are set; ZCL 6.3.2.1 requires an Alarms server on the endpoint.
- Not implemented: HVACSystemTypeConfiguration, RemoteSensing, TemperatureSetpointHold, TemperatureSetpointHoldDuration, the setpoint change tracking set (it needs UTC time), the setback attributes, EmergencyHeatDelta, the AC information set and Get Relay Status Log.
- ZHA 2.3.1 configures 11 reports on this cluster, creates the climate entity with the modes of ControlSequenceOfOperation, writes the occupied setpoints and SystemMode directly and never sends Setpoint Raise/Lower.

### Weekly schedule

- The schedule is the field `schedule` of `struct zzb_zcl_thermostat`, which the stack writes: for each of the 8 days of the Day of Week bitmap (Sunday to Saturday, bit 7 Away or Vacation), the `mode` of its sequence (bit 0 heat, bit 1 cool, 0 for a day without transitions), the `count` of transitions and up to `CONFIG_ZZB_ZCL_SCHEDULE_TRANSITIONS` (1 to 10, default 4) `transitions` of a `time` in minutes since midnight, a `heating` and a `cooling` setpoint, 0x8000 when the mode lacks it. The application carries out the schedule: it reads the schedule on the stack thread, takes the local time from `zzb_zcl_time_get()` (Time) and sets the setpoints with `zzb_zcl_attr_set()` while bit 0 of ThermostatProgrammingOperationMode is set.
- Set Weekly Schedule (ZCL 6.3.2.3.2): a payload shorter than 3 octets or than its transitions gives MALFORMED_COMMAND, a mode without bits 0 and 1 or no day INVALID_FIELD, more transitions than the capacity INSUFFICIENT_SPACE, a setpoint outside the absolute limits of its kind or a time above 1439 INVALID_VALUE, and two transitions with the same time FAILURE. After an error nothing is stored. Otherwise every day of the bitmap gets the sequence in place of its previous one, with bits 0 and 1 of the mode and the transitions in the order of the command. With at most 10 transitions a sequence fits in one command, so sequences sent in several commands are not supported.
- The Default Response follows the rule of Reception, although ZCL 6.3.2.3.2.8 asks for one in every case.
- Get Weekly Schedule (ZCL 6.3.2.3.3) answers a unicast with a Get Weekly Schedule Response for each requested day whose sequence has a requested mode bit, with the bit of that day and the mode bits that both have. Without a match it answers with 0 transitions and the requested days and mode. Clear Weekly Schedule clears every day of the endpoint.
- Each day with transitions has a record (section 15).

### Thermostat User Interface Configuration

- `ZZB_ZCL_THERMOSTAT_UI_SERVER(&v)`, ClusterRevision 1 (ZCL 6.6): TemperatureDisplayMode (0 to 1), KeypadLockout (0 to 5) and ScheduleProgrammingVisibility (0 to 1), writable and non-volatile, default 0; another value gives INVALID_VALUE. ZHA 2.3.1 creates a select for KeypadLockout.

### Fan Control

- `ZZB_ZCL_FAN_SERVER(&v)`, ClusterRevision 1 (ZCL 6.4). FanMode is writable, non-volatile and reportable, 0x00 to 0x06, default 0x05 (Auto); another value gives INVALID_VALUE. It is reportable although ZCL does not mark it: ZHA 2.3.1 configures its report. FanModeSequence is set by the application and read-only although ZCL makes it writable, for the reason given for ControlSequenceOfOperation.
- FanMode is not checked against FanModeSequence: ZCL gives no rule, and ZHA 2.3.1 writes 0x00 to 0x03 for a speed and 0x04 to 0x06 for a preset. The application resolves Smart (0x06) by occupancy. With a Thermostat server on the endpoint ZHA drives the fan from its climate entity with On and Auto.

### Pump Configuration and Control

- `ZZB_ZCL_PUMP_SERVER(&v)`, ClusterRevision 1 (ZCL 6.2). The application sets the maximum, constant and compensated limits, PumpStatus (reportable), EffectiveOperationMode, EffectiveControlMode, Capacity (reportable), Speed, Power (uint24), LifetimeEnergyConsumed (uint32, non-volatile) and AlarmMask (map16), all read-only; ZCL Table 6-4 and the text disagree on the access of Power and LifetimeEnergyConsumed. OperationMode (0 to 3), ControlMode (0 to 3, 5 and 7) and LifetimeRunningHours (uint24) are writable and non-volatile, default 0; another value gives INVALID_VALUE. The application counts LifetimeRunningHours with `zzb_zcl_attr_set()`.
- The stack has no Level Control for pumps (DTL Table 256). Without a Level Control server On/Off acts at once, as DTL Table 255 describes, and the application derives the setpoint from OperationMode and ControlMode. The application raises the pump alarms with `zzb_zcl_alarm()` when their bits of AlarmMask are set.
- ZHA 2.3.1 does not use this cluster.

### IAS Zone

- `ZZB_ZCL_IAS_ZONE_SERVER(&v)`, ClusterRevision 2 (ZCL 8.2). The application sets ZoneType (enum16) and ZoneStatus (map16), read-only, and NumberOfZoneSensitivityLevelsSupported, read-only, which reads as 2, its ZCL default, while the field is 0. The stack keeps ZoneState, IAS_CIE_Address (EUI64, writable), ZoneID (default 0xff) and CurrentZoneSensitivityLevel (writable), all non-volatile (ZCL 2.3.5). ZoneStatus is not reportable: ZCL marks it R, and ZHA 2.3.1 configures no report.
- The commands of the zone go to IAS_CIE_Address with `zzb_zcl_send_ext()`, to the source endpoint of the Write Attributes that set the address; ZCL 8.2.2.1.2.1 names no endpoint. The endpoint is an internal field with its own record (section 15).
- A write of IAS_CIE_Address is accepted from any device, which ZCL 8.2.2.1.2.1 leaves to the implementation. A new address ends the enrollment: ZoneState 0 and ZoneID 0xff, with `zcl_cb`. When the address is not 0 and not 0xffffffffffffffff, the zone sends Zone Enroll Request with ZoneType and the manufacturer code of the node descriptor after the Write Attributes Response (auto-enroll-request, ZCL 8.2.2.1.3). A write of the stored address changes nothing and sends nothing; ZHA 2.3.1 writes it again at every rejoin. A CIE that writes the stored address from another endpoint keeps the stored endpoint.
- ZCL 8.2.2.3.2.2 asks for Zone Enroll Request immediately after joining, the steps of ZCL 8.2.2.1.3 after the write of the address. The stack follows 8.2.2.1.3, since the request has no destination before the write.
- Zone Enroll Response with code 0 sets ZoneState 1 and ZoneID from the command; another code sets ZoneState 0 and ZoneID 0xff. An unsolicited response is accepted (auto-enroll-response).
- The commands of the server are accepted only from the network address to which APS resolves IAS_CIE_Address (ZCL 8.2.2.1.3). Other sources, and every source while the address is 0 or 0xffffffffffffffff or does not resolve, get NOT_AUTHORIZED (0x7e).
- A change of ZoneStatus, by `zzb_zcl_attr_set()` or by the test mode, sends Zone Status Change Notification (ZoneStatus, extended status 0, ZoneID and the delay) while ZoneState is 1, and Zone Enroll Request while ZoneState is 0 and the address is set (trip-to-pair). Without an address nothing is sent.
- A notification that APSDE-DATA refuses with `-ENOTCONN` or `-EBUSY` waits and is tried again 5 s after the last attempt or at the next change of ZoneStatus. One notification waits per endpoint and carries ZoneStatus at the time it is sent; its delay counts the quarter seconds since the first change not sent, at most 0xffff. A refused Zone Enroll Request of trip-to-pair waits in the same way, and ZoneState at the next attempt decides which of the two commands goes. After another error it counts as sent.
- Test mode (ZCL 8.2.2.2.2): Initiate Test Mode sets CurrentZoneSensitivityLevel from the command, sets bit 8 (Test) of ZoneStatus and ends after its duration in seconds; a duration of 0 ends it at once. The end and Initiate Normal Operation Mode clear bit 8. The stack keeps bit 8: a value set with `zzb_zcl_attr_set()` gets the bit of the test mode. The application learns the mode from the `zcl_cb` of ZoneStatus and runs its test behavior (ZCL 8.2.2.2.2.7).
- A CurrentZoneSensitivityLevel above NumberOfZoneSensitivityLevelsSupported gives INVALID_VALUE in a write and INVALID_FIELD in Initiate Test Mode. Level 0 is the default of the device and may equal another level (ZCL 8.2.2.1.2.4), so 0 to NumberOfZoneSensitivityLevelsSupported are accepted.
- Not implemented: periodic notifications (Supervision Notify, bit 4, which the application keeps 0), and the refusal of Bind and Unbind requests for IAS Zone from devices other than the CIE (ZCL 8.2.2.1.3).
- A sleepy zone polls at the long interval before enrollment, 5 s by default, which meets the 7 s of ZCL 8.2.2.1.3; a long interval above 7 s before enrollment departs from it. A hold until enrollment would poll at the short interval forever when no CIE enrolls the zone. A sent Zone Enroll Request calls `zzb_nwk_poll_fast()`, so the node fetches the response.
- The reset of the node sets ZoneState 0, ZoneID 0xff, IAS_CIE_Address 0 and CurrentZoneSensitivityLevel 0, ends the test mode, drops a waiting notification and deletes the record of the CIE endpoint.
- `ZZB_ZCL_IAS_ZONE_CLIENT()`, the client of the CIE, passes Zone Status Change Notification (6 octets) and Zone Enroll Request (4 octets) to `zcl_cmd_cb`; a shorter payload gives MALFORMED_COMMAND, and another command UNSUP_COMMAND. The application answers Zone Enroll Request with Zone Enroll Response through `zzb_zcl_cmd_respond()`. The stack has no client for Write Attributes, so a CIE built on it enrolls the zones whose IAS_CIE_Address a commissioning tool has written (ZCL 8.2.2.1.3).
- ZHA 2.3.1 binds IAS Zone to endpoint 1 of the coordinator, writes the IEEE address of the coordinator to IAS_CIE_Address, then sends Zone Enroll Response with code 0 and zone 0, answers every Zone Enroll Request in the same way and repeats this at every rejoin. It reads ZoneStatus and ZoneType and creates a binary sensor that is on with bit 0 or 1 of ZoneStatus, with a class from ZoneType: motion 0x000d, opening 0x0015, smoke 0x0028, moisture 0x002a, carbon monoxide 0x002b, vibration 0x002d.

### IAS WD

- `ZZB_ZCL_IAS_WD_SERVER(&v)`, ClusterRevision 2 (ZCL 8.4). MaxDuration is writable and non-volatile, default 240 s; 0xffff gives INVALID_VALUE.
- The fields of the first octet of the commands follow zigpy 2.3.0, since ZCL Figures 8-21 and 8-22 give no bit positions: warning mode in bits 4 to 7, strobe in bits 2 and 3, of which zigpy sets bit 2, and siren level in bits 0 and 1 of Start Warning; squawk mode in bits 4 to 7, strobe in bit 3 and squawk level in bits 0 and 1 of Squawk.
- Start Warning (ZCL 8.4.2.2.1): a warning mode above 6, a strobe above 1 or a strobe level above 3 gives INVALID_FIELD. The command goes to `zcl_cmd_cb` as a copy of its 5 octets with the warning duration limited to what is left of MaxDuration since the start of the continuous warning (ZCL 8.4.2.1.1) and the strobe duty cycle rounded to the nearest multiple of 10, 5 upwards, at most 100. The time left is rounded down to whole seconds, so that the warning does not last longer. When `zcl_cmd_cb` returns SUCCESS, warning mode 0 without strobe (Stop) ends the warning, and another command with a duration above 0 starts or continues it; a command that `zcl_cmd_cb` refuses, and one other than Stop with a duration of 0, leave the warning as it is. At the end of the duration the stack passes Start Warning with warning mode 0, no strobe and the other fields 0 to `zcl_cmd_cb`, in a `struct zzb_zcl_cmd` that it builds with `unicast` false and source address, source endpoint and TSN 0, and ignores the status.
- Squawk (ZCL 8.4.2.2.2): a squawk mode above 1 gives INVALID_FIELD. During a warning it has no effect and gets SUCCESS; otherwise it goes to `zcl_cmd_cb` as received.
- The reset of the node sets MaxDuration 240 and ends a warning with the Start Warning of the end.
- `ZZB_ZCL_IAS_WD_CLIENT()` is the client of the CIE; the server generates no commands.
- ZHA 2.3.1 creates a siren entity. It sends Start Warning with warning mode 3 (Emergency), siren level 2 and 5 s unless the user chooses otherwise, Start Warning with warning mode 0 to turn the siren off, and Squawk from its service. It does not read MaxDuration.

### IAS ACE

- `ZZB_ZCL_IAS_ACE_SERVER()` and `ZZB_ZCL_IAS_ACE_CLIENT()`, ClusterRevision 1 (ZCL 8.3), have no value struct: the cluster has no attributes.
- The server, on the CIE, checks the length of Arm, Bypass, Get Zone Information and Get Zone Status, including the character string of the arm/disarm code and the list of Bypass, and passes the commands to `zcl_cmd_cb`; a shorter payload gives MALFORMED_COMMAND. Emergency, Fire, Panic, Get Zone ID Map, Get Panel Status and Get Bypassed Zone List have no fields. The application keeps the zone table, the panel status, the codes and the delays, checks the values and answers with `zzb_zcl_cmd_respond()`; it sends Zone Status Changed, Panel Status Changed and Set Bypassed Zone List unsolicited with `zzb_zcl_send_bound()`.
- The client, on a keypad, checks the length of the nine commands of the server, including their lists and strings, and passes them to `zcl_cmd_cb`; a shorter payload gives MALFORMED_COMMAND, another command UNSUP_COMMAND. The keypad sends its commands with `zzb_zcl_send_bound()`.
- ZHA 2.3.1 is the IAS ACE server for a device with an IAS ACE client: it binds the client to the coordinator and creates an alarm control panel. It answers Arm with Arm Response and Panel Status Changed, Emergency, Fire and Panic with Panel Status Changed, and Get Panel Status with Get Panel Status Response; Bypass and the Get Zone commands get no answer. Disarming takes the code 1234 by default, arming takes none. It creates no entity for an IAS ACE server.

### Time

- The node has one clock: the UTC time at which it was set, `k_uptime_get()` at that moment and the offset of the local time. It is not saved, so after a reboot the node has no time until the clock is set again, and the reset of the node clears it. `zzb_zcl_time_get(node, &utc, &local)` returns the UTC and local time on the stack thread through `zzb_call_sync()`, or `-ENODATA` without time. The local time is LocalTime of the Time server of the node, or without a server the UTC time plus the offset of the last synchronization. Alarms and Door Lock take their time stamps from the clock, 0xffffffff without time.
- `ZZB_ZCL_TIME_SERVER(&v)`, ClusterRevision 2 (ZCL 3.12). Time (UTC) is the time of the clock; the `refresh` hook writes it, StandardTime and LocalTime before a read or a write, and they read as 0xffffffff without time. A write of Time and `zzb_zcl_attr_set()` set the clock, which keeps its offset, and LastSetTime; 0xffffffff gives INVALID_VALUE, and while bit 0 (Master) of TimeStatus is set, a write gets READ_ONLY (ZCL 3.12.2.2.1).
- The application sets TimeStatus; it is not non-volatile, since the clock is not. A write changes only bit 1 (Synchronized) and bit 3 (Superseding); bit 1 stays 0 while Master is set (ZCL Table 3-70).
- TimeZone and DstShift (int32, -86400 to 86400, otherwise INVALID_VALUE), DstStart, DstEnd and ValidUntilTime are writable and non-volatile, default 0 and 0xffffffff as in ZCL. While bit 2 (MasterZoneDst) of TimeStatus is set, a write of TimeZone or of the DST attributes gets READ_ONLY. StandardTime is Time plus TimeZone, LocalTime StandardTime plus DstShift while DstStart ≤ Time ≤ DstEnd (ZCL 3.12.2.2.3, 3.12.2.2.4).
- The reset of the node restores the defaults.
- `ZZB_ZCL_TIME_CLIENT()` sets the clock of the node from a Time server. The first endpoint with a Time client sends Read Attributes for Time, TimeStatus, TimeZone and LocalTime to the bound destinations of its client, or without a binding that resolves to 0x0000, endpoint 1, where zigpy-znp registers the Time server of the coordinator. The first request goes from the `start` hook of the client in `zzb_zcl_start()`, the next 60 s after a request without a valid response or after the reset of the node, which clears the clock, and 24 h after a valid response; one timer of the node serves them. A sent request calls `zzb_nwk_poll_fast()`, so a sleepy node fetches the response. On a sleepy node a request costs one frame and 3 s of fast polling, and the 60 s retry repeats only while no time server answers.
- A valid response has the TSN of the request, Time of type UTC with SUCCESS and other than 0xffffffff, and TimeStatus with Master or Synchronized set (ZCL 3.12.2.2.2). The clock takes Time and the offset LocalTime − Time, without LocalTime TimeZone, otherwise 0. A Read Attributes Response to the Time client gets SUCCESS by the rule of Reception, also when it is not valid.
- ZHA 2.3.1 does not use the cluster. zigpy answers a Read Attributes of the Time cluster when the endpoint of the device has a Time cluster: Time, TimeStatus 0x05 (Master, MasterZoneDst), TimeZone, StandardTime and LocalTime from the time zone of its host, UNSUPPORTED_ATTRIBUTE for the others.

### Door Lock

- `ZZB_ZCL_DOOR_LOCK_SERVER(&v)`, ClusterRevision 3 (ZCL 7.3). The application sets LockState and DoorState (reportable), LockType, ActuatorEnabled, MinPINCodeLength, MaxPINCodeLength (at most `ZZB_ZCL_DOOR_LOCK_PIN_MAX`, 8) and SupportedOperatingModes. NumberOfTotalUsersSupported and NumberOfPINUsersSupported are the constant `CONFIG_ZZB_ZCL_DOOR_LOCK_USERS` (default 8).
- The stack keeps, writable, non-volatile and reportable (RWP in ZCL): AutoRelockTime, OperatingMode (0 to 4, otherwise INVALID_VALUE), WrongCodeEntryLimit, UserCodeTemporaryDisableTime, SendPINOverTheAir, RequirePINforRFOperation, AlarmMask, KeypadOperationEventMask, RFOperationEventMask, ManualOperationEventMask, KeypadProgrammingEventMask and RFProgrammingEventMask. OperatingMode is not checked against SupportedOperatingModes: ZCL 7.3.2.12.8 says that a supported mode has its bit cleared, against the default 0x0001.
  - The event masks default to 0xffff, not to the 0x0000 of ZCL: with 0x0000 a coordinator that does not write them, such as ZHA 2.3.1, receives no event.
  - The other attributes of ZCL 7.3 are not implemented. The server has no scene extension field set, since a Door Lock should not support Groups and Scenes (DTL 14.4.1).
- Lock Door, Unlock Door, Toggle and Unlock with Timeout carry an optional code, an octet string; a payload without it is valid, as ZHA sends it, and a truncated code gives MALFORMED_COMMAND. The stack refuses the command with FAILURE when ActuatorEnabled is FALSE, OperatingMode is 2 (Privacy) or 3 (No RF Lock or Unlock) (ZCL Table 7-15), a lockout runs, or RequirePINforRFOperation is TRUE and the code is not the code of a user with status 1 (occupied, enabled) and a type other than 4 (non access).
- Otherwise Toggle becomes Unlock Door while LockState is 1 (locked) and Lock Door in the other states, and Unlock with Timeout becomes Unlock Door; a copy of the request with that ID and the received code goes to `zcl_cmd_cb`. The stack answers a unicast with the response of the received command (Lock Door Response, Unlock Door Response, Toggle Response, Unlock with Timeout Response): SUCCESS when `zcl_cmd_cb` returns SUCCESS, otherwise FAILURE, the only statuses of ZCL 7.3.2.16.1. When the application answers itself with `zzb_zcl_cmd_respond()`, the stack sends nothing.
- A wrong code counts, and so does a missing one. When WrongCodeEntryLimit is above 0 and the count reaches it, the RF commands are refused for UserCodeTemporaryDisableTime seconds, and alarm 0x04 occurs with bit 4 of AlarmMask (ZCL Table 7-22) through the Alarms server of the endpoint. A right code and the end of the lockout clear the count. The application counts the codes of its keypad.
- Unlock with Timeout for which `zcl_cmd_cb` returns SUCCESS sets a relock deadline after its timeout in seconds; a timeout of 0 acts as Unlock Door. When LockState becomes 2 (unlocked), the `changed` hook sets the deadline after AutoRelockTime seconds unless AutoRelockTime is 0 or a deadline is set, and so does an unlock command for which `zcl_cmd_cb` returns SUCCESS, since it does not change LockState of a lock that is already unlocked (ZCL 7.3.2.12.6: unlock operations from any source are timed); when LockState becomes 1, the deadline is removed. At the deadline the stack passes Lock Door to `zcl_cmd_cb` in a `struct zzb_zcl_cmd` that it builds with `unicast` false, source address, source endpoint and TSN 0 and no payload, ignores the status and sends the event AutoLock. One timer of the node serves the deadlines and lockouts of all endpoints.
- The users are the field `users` of the value struct, `CONFIG_ZZB_ZCL_DOOR_LOCK_USERS` entries of user status, user type, code length and code, which the stack writes and the application reads on the stack thread to check the codes of its keypad. Each occupied user has a record (section 15).
  - Set PIN Code gives status 1 (general failure) for a user ID outside the table, a user status other than 1 and 3, user type 1 or 2 (no schedules) or above 4, or a code length outside MinPINCodeLength to MaxPINCodeLength, and 3 (duplicate) for the code of another user (ZCL 7.3.2.16.6).
  - Get PIN Code, Get User Status and Get User Type answer with the user; an empty user has status 0, type 0xff and an empty code. A user ID outside the table gets a Default Response with INVALID_FIELD.
  - Clear PIN Code, Clear All PIN Codes, Set User Status (1 or 3) and Set User Type (0, 3 and 4; both fail for an empty user) answer with status 0 or 1.
  - While SendPINOverTheAir is FALSE, every code in a response or an event is replaced by 0xff octets of the same length (ZCL 7.3.2.13.3).
- Events go to the bound destinations of the server. Operation Event Notification with source 1 (RF) follows Lock Door (code 1) and Unlock Door (code 2) for which `zcl_cmd_cb` returns SUCCESS and the refusals for a wrong code (codes 3 and 5), with the bit of the code in RFOperationEventMask; its user ID is that of the user with the received code, or 0xffff. AutoLock has source 2 (manual), code 0x0a, user ID 0xffff, an empty code and bit 6 of ManualOperationEventMask. Programming Event Notification with source 1 follows Set PIN Code (code 2 for a new user, 4 for a changed code), Clear PIN Code and Clear All PIN Codes (code 3, user ID 0xffff for all) that pass, with the bit of the code in RFProgrammingEventMask; it carries the code, type and status of the user after Set PIN Code and before Clear PIN Code, and an empty code, type 0 and status 0 for all. LocalTime is the local time of the clock of the node or 0xffffffff, Data an empty string.
- Not implemented: Get Log Record, the schedule commands (0x0b to 0x13) and the RFID commands (0x16 to 0x19), which give UNSUP_COMMAND. The application sends the events of its keypad and manual operations with `zzb_zcl_send_bound()`.
- The reset of the node clears the users and their records, restores the defaults and removes the deadlines and lockouts.
- `ZZB_ZCL_DOOR_LOCK_CLIENT()` is the client of the Door Lock Controller.
- ZHA 2.3.1 creates a lock entity, binds the cluster and configures the report of LockState 0/900. It sends Lock Door and Unlock Door without a code and needs their responses with SUCCESS. Its user code services send Set PIN Code (user ID = slot − 1, status 1, type 0), Set User Status 1 or 3 and Clear PIN Code. Operation Event Notification becomes a `zha_event`.

### On/Off Switch Configuration

- `ZZB_ZCL_ON_OFF_SWITCH_CONFIG_SERVER(&v)`, ClusterRevision 1 (ZCL 3.9). The application sets SwitchType. SwitchActions is writable and non-volatile, 0 to 2, default 0. The application sends the On/Off commands of ZCL Table 3-54 with `zzb_zcl_send_bound()` from the On/Off client of the endpoint, which ZCL 3.9.2.1 requires. ZHA 2.3.1 does not use the cluster.

### Binary Input

- `ZZB_ZCL_BINARY_INPUT_SERVER(&v)`, Binary Input (Basic), ClusterRevision 1 (ZCL 3.14.5). The application sets ActiveText, Description and InactiveText (character strings, read-only, which R*W allows), Polarity, Reliability, ApplicationType and PresentValue and StatusFlags, which are reportable (ZCL 3.14.5.4.4). OutOfService is writable and non-volatile. PresentValue is writable while OutOfService is TRUE, otherwise READ_ONLY (ZCL 3.14.11.2).
- The stack keeps bit 3 (OUT_OF_SERVICE) of StatusFlags equal to OutOfService and bit 1 (FAULT) set while Reliability is not 0, also in a value set with `zzb_zcl_attr_set()` (ZCL 3.14.11.3). StatusFlags changes with `zcl_cb` when OutOfService changes, also by `zzb_zcl_attr_set()`, and without it when the application sets Reliability or StatusFlags.
- The application applies Polarity to its input and stops setting PresentValue while OutOfService is TRUE; it learns the change from `zcl_cb`.
- ZHA 2.3.1 creates a binary sensor, named by Description, binds the cluster and configures the report of PresentValue 0/900.

### Metering

- `ZZB_ZCL_METERING_SERVER(&v)`, ClusterRevision 2 (ZCL 10.4). The application sets CurrentSummationDelivered (uint48), Status and InstantaneousDemand (int24), which are reportable, UnitofMeasure, Multiplier, Divisor, SummationFormatting, DemandFormatting and MeteringDeviceType. The commands, all optional, are not implemented.
- CurrentSummationReceived is not implemented: ZHA 2.3.1 creates an entity and a report for it, and a Smart Plug would then fill the 16 entries of the reporting table.
- ZHA 2.3.1 creates sensors for InstantaneousDemand and CurrentSummationDelivered unless UnitofMeasure is 0xff, binds the cluster and configures the reports of InstantaneousDemand 5/900/1, CurrentSummationDelivered 30/900/1 and Status 1/900. It shows value × Multiplier ÷ Divisor; for unit 0 the demand in W (× 1000) and the summation in kWh.

### Meter Identification

- `ZZB_ZCL_METER_ID_SERVER(&v)`, ClusterRevision 2 (ZCL 10.13). The application sets all attributes. CustomerName is read-only, since the strings of the stack are. ZHA 2.3.1 does not use the cluster.

### Electrical Measurement

- `ZZB_ZCL_ELECTRICAL_MEASUREMENT_SERVER(&v)`, ClusterRevision 3 (ZCL 4.9). The application sets MeasurementType, ACFrequency, ACFrequencyMin and ACFrequencyMax, the phase A set RMSVoltage, RMSVoltageMin, RMSVoltageMax, RMSCurrent, RMSCurrentMin, RMSCurrentMax, ActivePower, ActivePowerMin, ActivePowerMax, ReactivePower, ApparentPower and PowerFactor, ACFrequencyMultiplier and ACFrequencyDivisor, and the multipliers and divisors of AC voltage, current and power. ACFrequency, the phase A measurements without their minimum and maximum and without PowerFactor, which ZCL makes read-only, and the multipliers and divisors are reportable; ZHA 2.3.1 configures the reports of ACFrequency, RMSVoltage, RMSCurrent, ActivePower, ApparentPower and the AC multipliers and divisors.
- Not implemented: the DC, harmonic, total power, phase B and C, average RMS and alarm sets, and the profile commands.
- ZHA 2.3.1 creates sensors for ActivePower, ApparentPower, RMSCurrent, RMSVoltage, ACFrequency and PowerFactor, binds the cluster, configures the reports of the multipliers and divisors 0/900/1 and of the measurements 5/900/1, and reads the measurements every 30 to 45 s. On a Smart Plug with Metering and On/Off it configures 15 reports.

### Power Profile

- `ZZB_ZCL_POWER_PROFILE_SERVER(&v)`, ClusterRevision 1 (ZCL 3.17). The application sets TotalProfileNum, MultipleScheduling, EnergyFormatting and EnergyRemote. ScheduleMode is writable, non-volatile and reportable; bits 2 to 7 give INVALID_VALUE, and the reset of the node sets it to 0. The power profiles, energy phases, states and schedules are in the application: the commands 0x00 to 0x08 go to `zcl_cmd_cb` after the length checks, the application answers them with `zzb_zcl_cmd_respond()` and sends the notifications with `zzb_zcl_send_bound()`.
- `ZZB_ZCL_POWER_PROFILE_CLIENT()`, on a Home Gateway, passes the commands 0x00 to 0x0b of the server to `zcl_cmd_cb` after the length checks. GetPowerProfilePriceExtended with bit 0 of Options and without the 2 octets of PowerProfileStartTime gives MALFORMED_COMMAND.
- ZHA 2.3.1 does not use the cluster.

### EN50523 appliance clusters

- `ZZB_ZCL_APPLIANCE_CONTROL_SERVER(&v)`, EN50523 Appliance Control, ClusterRevision 1 (ZCL 15.2): the application sets StartTime, FinishTime and RemainingTime, which are reportable. Execution of a Command, Signal State, Write Functions, Overload Pause Resume, Overload Pause and Overload Warning go to `zcl_cmd_cb`; the stack checks the typed records of Write Functions by the lengths of their types, and a truncated record or a type of unknown length gives MALFORMED_COMMAND. Overload Warning takes the one octet of its enum8 Warning Event, although ZCL Figure 15-5 gives 2. `ZZB_ZCL_APPLIANCE_CONTROL_CLIENT()` passes Signal State Response and Signal State Notification; Appliance Status 2 follows the 2 octets of the fixed fields when the payload has 5 octets or more.
- `ZZB_ZCL_APPLIANCE_ID_SERVER(&v)`, EN50523 Appliance Identification, ClusterRevision 1 (ZCL 15.3): the application sets BasicIdentification (uint56) and the attributes of the extended set.
- `ZZB_ZCL_APPLIANCE_EVENTS_SERVER()`, EN50523 Appliance Events and Alerts, ClusterRevision 1 (ZCL 15.4), has no value struct: Get Alerts goes to `zcl_cmd_cb`. `ZZB_ZCL_APPLIANCE_EVENTS_CLIENT()` passes Get Alerts Response, Alerts Notification and Event Notification.
- `ZZB_ZCL_APPLIANCE_STATS_SERVER(&v)`, Appliance Statistics, ClusterRevision 1 (ZCL 15.5): the application sets LogMaxSize and LogQueueMaxSize; Log Request and Log Queue Request go to `zcl_cmd_cb`. `ZZB_ZCL_APPLIANCE_STATS_CLIENT()` passes Log Notification, Log Response, Log Queue Response and Statistics Available; a log payload shorter than its Log Length gives MALFORMED_COMMAND.
- The application keeps the state of the appliance, answers with `zzb_zcl_cmd_respond()` and sends the notifications with `zzb_zcl_send_bound()`. ZHA 2.3.1 does not use these clusters.

### Poll Control

- `ZZB_ZCL_POLL_CONTROL_SERVER(&v)`, ClusterRevision 3 (ZCL 3.16), for a sleepy end device. All intervals are in quarter seconds. Check-inInterval and FastPollTimeout are writable and non-volatile. LongPollInterval and ShortPollInterval are read-only and non-volatile; Set Long Poll Interval, Set Short Poll Interval and `zzb_zcl_attr_set()` change them. No attribute is reportable.
- The defaults are Check-inInterval 0x3840 (1 h), LongPollInterval `CONFIG_ZZB_NWK_LONG_POLL_MS` / 250, ShortPollInterval `CONFIG_ZZB_NWK_SHORT_POLL_MS` / 250 and FastPollTimeout 0x28 (10 s). With the default Kconfig values LongPollInterval and ShortPollInterval are the ZCL defaults 0x14 and 0x02.
- The application sets Check-inIntervalMin, LongPollIntervalMin and FastPollTimeoutMax; 0 means no limit. For FastPollTimeoutMax this departs from ZCL, whose default 0 as a maximum would refuse every fast poll timeout.
- INVALID_VALUE answers (ZCL 3.16.4, 3.16.5.5, 3.16.5.6):
  - a write of Check-inInterval above 0x6E0000, below Check-inIntervalMin or below LongPollInterval; 0 turns the check-ins off and is always accepted;
  - a write of FastPollTimeout of 0 or above FastPollTimeoutMax;
  - Set Long Poll Interval outside 4 to 0x6E0000, below LongPollIntervalMin or ShortPollInterval, or above a Check-inInterval other than 0;
  - Set Short Poll Interval of 0 or above LongPollInterval.

  The two commands answer with a Default Response, and `zzb_zcl_attr_set()` returns `-EINVAL` for these values.
- The first endpoint with a Poll Control server sets the intervals of NWK with `zzb_nwk_set_poll_intervals()` from its `start`, `changed` and `reset` hooks. Other servers keep their attributes and check in, and they do not change the intervals.
- Check-in goes every Check-inInterval to the bound destinations with the function of Clients (Disable Default Response 1, APS ACK request). The first check-in follows one interval after `zzb_zcl_start()`; nothing is sent at a join. A new Check-inInterval, from a write or `zzb_zcl_attr_set()`, restarts the interval (ZCL 3.16.6.3), while responses and Fast Poll Stop do not shift it. A check-in that cannot be sent, without a binding or a network, opens no window, and the next one follows after the interval.
- A sent check-in opens a window of 7.68 s (ZCL 3.16.4.4), during which the server holds the fast poll rate with `ZZB_NWK_HOLD_ZCL`. The client table of an endpoint has 4 entries with the network address, the endpoint and the deadline of a client; a full table replaces the entry that ends first. The hold lasts until the window and every deadline have ended, so fast polling lasts until the latest deadline (ZCL 3.16.6).
- Check-in Response (ZCL 3.16.5.3):
  - a source that `zzb_aps_bound()` does not find among the bindings of Poll Control on the endpoint gets a Default Response FAILURE, and so does a response outside the window;
  - a Fast Poll Timeout above FastPollTimeoutMax gets INVALID_VALUE, as a write of FastPollTimeout does; ZCL also suggests INVALID_FIELD;
  - Start Fast Polling TRUE sets the deadline of the client to now plus Fast Poll Timeout, where 0 means FastPollTimeout; FALSE removes the deadline of the client.

  Whether a Default Response SUCCESS follows depends on Disable Default Response (Reception).
- Fast Poll Stop (ZCL 3.16.5.4): an unbound source gets FAILURE, and so does every source while the server does not fast poll. Otherwise the deadline of the client is removed; while other clients have deadlines the response is FAILURE, otherwise the window ends too, the server stops fast polling and the response is SUCCESS.
- One timer of the node serves the next check-in, the end of the window and the deadlines of the clients of every endpoint. The reset of the node restores the defaults, clears the client tables and the windows, and ends the hold.
- `ZZB_ZCL_POLL_CONTROL_CLIENT()` passes Check-in to `zcl_cmd_cb`; the application answers with Check-in Response through `zzb_zcl_cmd_respond()`.
- ZHA 2.3.1 leaves Poll Control to zigpy 2.3.0. zigpy answers Check-in with Check-in Response with Disable Default Response 1 and the TSN of the check-in: Start Fast Polling TRUE and 120 quarter seconds during the interview of the device or while its requests wait, FALSE otherwise. During the interview it binds Poll Control and writes FastPollTimeout 120, but only when the cluster is on the first endpoint, and FastPollTimeoutMax must then be 0 or at least 120. zigpy does not write Check-inInterval and sends neither Fast Poll Stop nor the Set commands. It sends a Default Response to a check-in whose Disable Default Response is 0.

### OTA Upgrade

- `CONFIG_ZZB_ZCL_OTA` (default n) builds `clusters/ota.c`. It depends on `CONFIG_MCUBOOT_IMG_MANAGER`, so the application enables `CONFIG_STREAM_FLASH` and `CONFIG_IMG_MANAGER`, and it selects `CONFIG_IMG_ERASE_PROGRESSIVELY`: an erase of the whole upload slot before a download would block the stack thread for seconds (472 KiB on nRF52840, 1792 KiB on ESP32-H2). Without the option the stack does not use `flash_img` or MCUboot, a firmware without MCUboot builds as before, and `ZZB_ZCL_OTA_CLIENT()` fails to link. The image manager does not need a bootloader, so the tests run on native_sim and write the upload slot of the flash simulator.
- `ZZB_ZCL_OTA_CLIENT(&v)`, ClusterRevision 4 (ZCL 11.3.1.1), is the client of a device whose firmware MCUboot boots. The application sets `current_file_version` (CurrentFileVersion), for example to APPVERSION of its `VERSION` file, and `image_type`, 0x0000 to 0xffbf. The stack keeps FileOffset, DownloadedFileVersion, ImageUpgradeStatus and ImageTypeID with the defaults of ZCL Table 11-10, and every change of them comes with `zcl_cb`. The `refresh` hook writes UpgradeServerID (apsTrustCenterAddress) and ManufacturerID (the manufacturer code of the device definition), and CurrentZigBeeStackVersion is the constant 0x0002. All attributes are read-only. DownloadedZigBeeStackVersion, MinimumBlockPeriod, Image Stamp, the policies, Image Page Request and the device specific file commands are not implemented.
- The first endpoint with an OTA client downloads; a client on another endpoint has the attributes only, and its commands get UNSUP_COMMAND. The server is the trust center at network address 0x0000, and the client sends no Match_Desc_req (ZCL 11.8). Commands from other sources are ignored. The server endpoint is the source endpoint of the last Image Notify that started a query, 1 before the first and after the reset of the node.
- Query Next Image Request, Image Block Request and Upgrade End Request go to the server with Disable Default Response 0 (ZCL Table 11-16) and an APS ACK request. A Default Response of the server is dropped (Reception), NO_IMAGE_AVAILABLE to an Image Block Request included; the retries below end such a download.
- Query Next Image Request carries the manufacturer code, the image type and CurrentFileVersion, without a hardware version. The first follows one `CONFIG_ZZB_ZCL_OTA_QUERY_INTERVAL` (minutes, default 1440) after `zzb_zcl_start()`, and the next ones follow every interval while ImageUpgradeStatus is Normal; nothing is sent at a join. A query that APSDE-DATA refuses, off a network or during a rejoin, is tried again after 60 s. A sent query calls `zzb_nwk_poll_fast()`.
- Image Notify (ZCL 11.13.3.4) while ImageUpgradeStatus is Normal: a unicast starts a query at once. A broadcast is ignored when it is malformed (payload type above 3, QueryJitter outside 1 to 100, a payload shorter than its type), when its manufacturer code or image type is neither that of the device nor 0xffff, or, with payload type 3, when its file version is CurrentFileVersion or DownloadedFileVersion and not 0xffffffff. Otherwise a query starts when a random number from 1 to 100 is at most QueryJitter. A query restarts the interval.
- Query Next Image Response with the TSN of the last query (ZCL 11.13.5): NO_IMAGE_AVAILABLE, NOT_AUTHORIZED and a SUCCESS with the file version CurrentFileVersion or DownloadedFileVersion end the query. A SUCCESS shorter than 13 octets, with another manufacturer code or image type, or with an image size below the 56 octets of the OTA header gets MALFORMED_COMMAND. Another SUCCESS starts the download: ImageUpgradeStatus 1, ImageTypeID the image type, DownloadedFileVersion the offered file version and FileOffset 0.
- The firmware has one upload slot, the one that `flash_img_init()` opens (slot1 while the code runs from slot0), so one node of a binary downloads at a time. The context of `flash_img` and the parser of the file are static in `ota.c` with the node that owns them (section 3); a SUCCESS while another node owns them is ignored.
- Image Block Request asks for `CONFIG_ZZB_ZCL_OTA_BLOCK_SIZE` octets (default 48, at most 65) at FileOffset. An Image Block Response has 17 octets besides the data, so 65 octets fill an ASDU of 82 octets with NWK security only, and 48 fit one with APS security.
- Image Block Response with the TSN of the last request (ZCL 11.13.8):
  - SUCCESS with the manufacturer code, image type, file version and file offset of the request, a data size from 1 to the requested size, exactly that many octets of data and an end within the image size: the data go to the parser, FileOffset advances, and the next request follows at once. Another SUCCESS gets MALFORMED_COMMAND and is otherwise ignored.
  - WAIT_FOR_DATA: the same request follows after RequestTime seconds when CurrentTime is 0, otherwise after RequestTime − CurrentTime seconds. A payload shorter than 9 octets or a RequestTime below a CurrentTime other than 0 gets MALFORMED_COMMAND. The MinimumBlockPeriod field is ignored.
  - ABORT ends the download.
- A request without a response is sent again after 5 s, and the download ends after three repetitions without a response. zigpy ends an install after 30 s without a block request.
- The parser checks the OTA header (ZCL 11.4.2) when its 56 octets have arrived: the identifier 0x0BEEF11E, a header version 0x01xx, the manufacturer code, image type and file version of the Query Next Image Response and a total image size equal to the image size of the response. It skips the header up to its header length and reads the sub-elements (ZCL 11.4.3), whose tags and lengths may cross blocks. The data of the Upgrade Image sub-element (tag 0x0000) go to the upload slot with `flash_img_buffered_write()`; other tags are skipped. A header that does not match, a second tag 0x0000, a tag 0x0000 that is empty or longer than the slot and a sub-element that passes the end of the file give INVALID_IMAGE at once.
- After the last block the image is valid when the file ends with a complete sub-element, has a tag 0x0000, and the upload slot starts with an MCUboot image header (`boot_read_bank_header()`). The client then sends Upgrade End Request SUCCESS, and ImageUpgradeStatus becomes 2. MCUboot checks the hash or signature of the image at the next boot.
- A download ends with Upgrade End Request INVALID_IMAGE after an invalid image, with ABORT after a flash error, and without a frame after an ABORT of the server, after the retries, at the reset of the node and in `zzb_zcl_init()`. ImageUpgradeStatus becomes Normal, FileOffset, DownloadedFileVersion and ImageTypeID take their defaults, the upload slot is released, and the next query follows one interval later. MCUboot ignores the partial image in the slot, since the first write erases the page of its trailer.
- Upgrade End Response (ZCL 11.13.9.6) is processed in ImageUpgradeStatus 2, 3 and 4, also unsolicited or broadcast, when its manufacturer code, image type and file version are those of the download or the wildcards 0xffff, 0xffff and 0xffffffff. Other responses are ignored, such as the one that zigpy sends to INVALID_IMAGE. A payload shorter than 16 octets gets MALFORMED_COMMAND.
  - UpgradeTime 0xffffffff: ImageUpgradeStatus 3, and Upgrade End Request SUCCESS is sent again every 60 minutes (ZCL 11.16).
  - Otherwise the delay is UpgradeTime seconds when CurrentTime is 0, else UpgradeTime − CurrentTime; an UpgradeTime below CurrentTime gets MALFORMED_COMMAND. ImageUpgradeStatus is 4 during the delay, which lasts at least 1 s, so that the APS acknowledgement of the response is sent before the application reboots in `ota_cb`; ZHA otherwise reports the update as failed.
  - An Upgrade End Request without a response is repeated as a block request is, and after three repetitions the client waits as for 0xffffffff.
- At the upgrade time the client calls `boot_request_upgrade(BOOT_UPGRADE_TEST)` and, when it returns 0, `ota_cb(node)` of `struct zzb_node_config` on the stack thread. ImageUpgradeStatus stays 4, the node releases the upload slot, and the client ignores Upgrade End Responses and sends nothing more. An error of `boot_request_upgrade()` is a flash error and ends the download. The request stays in the trailer of the upload slot until the next boot, also across the reset of the node, so without `ota_cb` the image is installed at the next reboot. The application reboots, for example with `sys_reboot()`, and confirms the new image with `boot_write_img_confirmed()` once it runs, for example in its BDB callback at ZZB_BDB_EVENT_INIT with ZZB_BDB_SUCCESS. In the swap modes MCUboot reverts an image that is not confirmed at the next reset; the overwrite-only mode, the default of ESP32, does not revert.
- The client holds the fast poll rate with `ZZB_NWK_HOLD_OTA` while it waits for an Image Block Response or an Upgrade End Response. One timer of the node serves the queries, the response timeouts, WAIT_FOR_DATA, the count down and the hourly Upgrade End Request.
- The flash operations run on the stack thread while an Image Block Response is processed: a write of `CONFIG_IMG_BLOCK_BUF_SIZE` (512) octets for every 512 octets of image and the erase of a page before its first write. The next request and the APS acknowledgement of the response (section 11) follow them.
- The reset of the node restores the defaults with `zcl_cb` and restarts the query interval.
- `scripts/zigbee_ota.py` builds an OTA file from `zephyr.signed.bin` with the Python standard library: the 56-octet header with header version 0x0100, field control 0, ZigBee stack version 0x0002 and the total image size, and one Upgrade Image sub-element with the signed image. It refuses an input without an MCUboot image header. Its arguments are the manufacturer code, the image type and the file version, which is the CurrentFileVersion of the new image.
- With `--index <file>` the script also writes the entry of the OTA file into the index file of the `zigpy_local` provider: the path relative to the directory of the index, the file version, the file size, the image type, the manufacturer code and the SHA3-256 checksum. An entry with the same manufacturer code, image type and file version is replaced; a missing index file is created.
- zigpy 2.3.0, the OTA server of ZHA 2.3.1, reads CurrentFileVersion and sends a unicast Image Notify (payload type 0, QueryJitter 100) at the interview of a device, and broadcasts one every 3.9 h. It answers a query outside an install with NO_IMAGE_AVAILABLE and an Image Block Request outside an install with ABORT. To install an update it binds Poll Control and writes FastPollTimeout when the device has a Poll Control server, sends a unicast Image Notify without waiting for a check-in, and waits 30 s for the query, which it answers with SUCCESS and the values of the file header. It sends blocks of at most 50 octets and never WAIT_FOR_DATA. The install ends after 30 s without a block request, and 60 s after the last block without an Upgrade End Request. zigpy answers every Upgrade End Request with an Upgrade End Response with CurrentTime 0 and UpgradeTime 0, and after a success it reads CurrentFileVersion 10 s later, up to 10 times 10 s apart. It sends to the first endpoint with an OTA client, with direction 1, Disable Default Response 1 and NWK security only, and accepts any OTA file that starts with the identifier 0x0BEEF11E. The `zigpy_local` provider of the Home Assistant configuration offers the files of an index file.

### Initialization and reset

- `zzb_node_init()` calls `zzb_zcl_init()` after `zzb_zdo_init()`; it cancels the timers of the node (Identify, On/Off, reporting, Level Control, Color Control, mains voltage and device temperature alarms, IAS Zone notifications and test mode, IAS WD warnings, Door Lock deadlines and lockouts, Time client, Poll Control, OTA Upgrade client) and calls the `init` hooks. The settings are loaded next (section 15), and `zzb_zcl_start()` calls the `start` hooks and sends the first request of the Time client after the frame counters have started and before `zzb_bdb_init()`.
- `zzb_zcl_reset()`, called by the reset of the node (section 13), calls the `reset` hooks, clears the reporting, scene and alarm tables and the clock of the node, stops the reporting timer that the hooks may have armed, and then deletes the records. The hooks set the defaults in the same way as a remote change, so the timers and transitions stop and `zcl_cb` reports every value that changes: OnOff, IdentifyTime, OnTime and OffWaitTime become 0, GlobalSceneControl TRUE, StartUpOnOff `start_up_default`, and Level Control, Color Control and the writable attributes of the other clusters take their defaults. StartUpOnOff is not applied, since the reset is not a power-up.

## 17. Sleepy end device and power management

- `sleepy` in `struct zzb_node_config` makes a sleepy end device (section 3). The stack turns the receiver off outside MLME procedures (section 9) and arms only deadline timers. Zephyr tickless idle and, with `CONFIG_PM`, the PM policy put the CPU to sleep between them.
- The stack holds a lock of all PM states with `pm_policy_state_all_lock_get()` while the radio works: during a transmission (section 8), while a sleepy node runs an MLME procedure, and as long as a node that is not sleepy exists (section 9). The stack cannot know which PM states of a SoC stop the radio, so it locks all of them. The waits between polls, the keepalive and the timers of ZCL hold no lock. Without `CONFIG_PM` the lock functions are empty. The application enables `CONFIG_PM` and the wakeup source of its SoC, and takes its own locks for work that must run without sleep.
- Zephyr v4.4.2: nrf5 and esp32 support RxOnWhenIdle and `stop()` (section 8). On ESP32-H2 the PM policy enters only `light_sleep` (`PM_STATE_STANDBY`, min-residency 1000 us), and `soc/espressif/common/power.c` enters it only with `CONFIG_PM_DEVICE`, `CONFIG_COUNTER` and `&rtc_timer { status = "okay"; wakeup-source; };` and when the next kernel timeout is at least 1.2 ms away; it wakes 200 us before the timeout. The esp32 driver takes no PM lock, and the 802.15.4 MAC retention of the Espressif HAL is built only with the ESP-IDF symbol `CONFIG_PM_ENABLE`, which the Zephyr port does not define. Without a retention module `modem_domain_pd_allowed()` of the HAL returns true, so light sleep powers the modem domain down and the radio loses its state. The application keeps the domain on with `esp_sleep_pd_config(ESP_PD_DOMAIN_MODEM, ESP_PD_OPTION_ON)` before `zzb_node_init()`. `CONFIG_IEEE802154_ESP32_SLEEP_ENABLE` does not exist in this version; it comes with a later esp32 driver (upstream commit eaabae51451), which takes PM locks and depends on `NET_L2_OPENTHREAD`. `esp_ieee802154_sleep()` keeps the PHY powered, since `IEEE802154_RF_DISABLE` is empty without `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, so without `CONFIG_PM` the saving on ESP32 is the current of the receiver.
- Hardware check on ESP32-H2 against ZHA with a Z-Stack coordinator, with `CONFIG_PM`: with the modem domain powered down the node found no network, with and without the locks of the stack. With the modem domain on, in 35 minutes each with and without the locks, the node joined at the first attempt, ZHA configured it, Identify arrived, 34 of 34 IAS Zone notifications reached zigpy, and the node did not rejoin. The application counted about 38 calls of `pm_state_set()` per second; whether the CPU sleeps during the receive windows was not measured.
- A zero-length data frame from the parent means no pending data (section 8).
- Poll rate (BDB 6.8): the scheduler of NWK (section 10) polls at the long interval, and at the short one while a layer holds the fast rate: the APS acknowledgement wait, the transactions of the ZDO client, the BDB steps that wait for frames, finding & binding, and the check-in window and the clients of Poll Control. It also polls at the short interval for 3 s after a delivered data frame and after a request of ZCL. A parent keeps a buffered frame for 7.5 s (BDB 6.8), 7.68 s in ZCL 3.16.2.
- An End Device Timeout Request follows every join and rejoin, and a sleepy node fetches the response with a poll. The timeout is `CONFIG_ZZB_NWK_ED_TIMEOUT` (256 min), and the keepalive (timeout / 3) keeps it for any long poll interval (section 10).
- The Poll Control server sets the poll intervals and fast polls for its clients (section 16).
- The OTA Upgrade client holds the fast poll rate while it waits for a response, so a download takes one block per short poll interval or less (section 16). zigpy 2.3.0 sends the Image Notify of an install without waiting for a check-in, so a sleepy node receives it only when it polls while its parent keeps the frame.
- A sleepy node drops frames to 0xfffd and multicast frames (section 10), so it has no group memberships; BDB 6.6 requires them only of a node that is not sleepy.
- Parent loss, reported by NWK after a keepalive failure or `CONFIG_ZZB_NWK_PARENT_FAILURES` failed transmissions (section 10): BDB rejoins securely on the current channel, then securely on all channels, then with a trust center rejoin, and repeats the series with a growing pause (section 13). This also recovers from a channel change missed while asleep.

## 18. Extension points for coordinator and trust center

- Join, rejoin and leave are split into joiner-side and parent-side files sharing the frame codecs. Parent-side files are compiled with `zephyr_library_sources_ifdef(CONFIG_ZZB_ROUTER_CAPABLE ...)`. Small branches use `IS_ENABLED()`; functions contain no `#ifdef`.
- The parent has one "device joined" path. It sends APS Update Device to a remote trust center, or calls the local trust center through `node->tc` ops, which are NULL when `CONFIG_ZZB_TRUST_CENTER` is off. Children of the trust center itself never produce an Update Device frame on air. Received Request Key and Verify Key commands use the same dispatch.
- APS Tunnel is part of the router (milestone 21): a remote trust center reaches a joining grandchild through it.
- Public API tiers: (1) raw APSDE and endpoint registration with a raw handler; (2) ZDO client; (3) ZCL and BDB, including `zzb_zcl_attr_set()`, `zzb_zcl_send_bound()`, `zzb_zcl_alarm()`, `zzb_zcl_cmd_respond()`, `zzb_zcl_time_get()` and the callbacks `zcl_cb`, `zcl_effect_cb`, `zcl_cmd_cb` and `ota_cb` (section 16). An NCP build uses tiers 1 and 2 plus configuration get/set.
- Export and import of keys, frame counters and the device table are defined together with persistence, because zigpy relies on network backup and restore.
- NCP framing is independent of the transport (UART, USB CDC, TCP). Protocol candidates: ZBOSS NCP (zigpy-zboss exists), Z-Stack MT/ZNP (zigpy-znp exists), or a new protocol with a new zigpy radio library. The choice is made at milestone 24.

## 19. Reference code

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

## 20. Testing

- Unit tests: ztest on `native_sim/native/64`; CI also runs `native_sim` (32-bit) to catch pointer-size errors.
- Crypto and codec tests use R22 Annex C vectors, BDB 10.1 install code examples and frames captured from real networks. The first frames come from the Ziggurat and zigbee-rs test suites (listed in `NOTICE`) and from a test network whose network key may be published. MAC frames missing from these sources are built from the IEEE 802.15.4-2011 figures and R22 Tables D-1 to D-3.
- Multi-node integration tests: fake radio devices implementing `ieee802154_radio_api` (`tests/common/fake_radio.c`), connected by an in-process medium. Each fake radio is a net device for the custom-L2 backend with its own `net_if`, since the raw backend serves one port. The medium has a link matrix, delivers a frame only to radios on the same channel whose receiver is on and filters by PAN ID and destination address; frame loss is added with the router MAC (milestone 20). It logs every frame with `LOG_HEXDUMP_DBG`. The MAC calls the radio API in the same way as on hardware. native_sim time is simulated, so timer behavior is deterministic. A fake radio has RxOnWhenIdle, TRUE by default, and a stopped state: `start()` and `stop()` set the state, `configure()` writes RxOnWhenIdle, and a TX leaves the receiver on only with RxOnWhenIdle TRUE. The replies that the scripted parent sends inside the `tx()` of a sleepy child therefore arrive only during an MLME procedure of the child.
- A test can replace the interface of a fake radio by a callback, which receives the frames the radio accepts and returns the frame pending bit of the ACK, and can send frames from that radio with `fake_radio_send()`.
- `tests/glue/raw` tests the radio port in raw mode against FFF fakes of the radio API: TX modes, software CSMA-CA, status mapping, frame pending from the ACK and the RX copy. A second scenario turns `CONFIG_ZZB_RADIO_TX_BACKOFF` on and checks the waits in kernel time. `tests/glue/custom_l2` tests the custom-L2 backend with two fake radios.
- `tests/unit/mac` tests the device MAC in raw mode against FFF fakes of the radio API. The `tx()` fake plays the parent: it returns the ACK through `ieee802154_handle_ack()`, with frame pending when the test sets it, and injects replies with `net_recv_data()`. It simulates frame loss by withholding the ACK. A second scenario builds the suite with `CONFIG_PM`: the test selects `HAS_PM` and adds a `standby` power state that no CPU lists, so the PM policy never enters it, and the tests check the PM lock at each transmission, during a poll and after each test.
- `tests/unit/nwk` tests NWK with the MAC and APS turned off, against FFF fakes of MCPS, MLME and the NLDE and NLME callbacks. Node 1 plays the parent: the test builds its frames, secured commands included, with the NWK send functions and passes the frames caught by the MCPS fake to node 0.
- `tests/unit/aps` tests APS with NWK and ZDO turned off, against FFF fakes of NLDE-DATA.request, the NWK functions APS reads and the APSDE and APSME callbacks. Node 0 has the destination address of the captured Transport Key and node 1 its source address. Node 1 plays the trust center and secures its frames with `zzb_aps_secure()` and explicit keys. Node 2 has the install code of BDB 10.1.
- `tests/unit/zdo` tests ZDO with BDB, ZCL, APS and NWK turned off, against FFF fakes of APSDE-DATA.request, the APSME, NLME, BDB and ZCL functions ZDO calls and the confirm of the ZDO client. Its endpoints have cluster instances without definitions. The test fills the neighbor and binding tables of the node directly, passes requests to `zzb_apsde_data_ind()` and confirms to `zzb_apsde_data_conf()`, and checks the frames caught by the APSDE fake.
- `tests/unit/bdb` tests BDB with ZDO, ZCL, APS and NWK turned off, against FFF fakes of the NLME, APSME, ZDO client and ZCL functions BDB calls. The event callback of the test records the events. The test calls the NLME, APSME and ZDO callbacks of BDB directly; the pauses and timeouts run in kernel time. Its device has endpoints with cluster instances without definitions for finding & binding.
- `tests/unit/zcl` tests ZCL with BDB, ZDO, APS and NWK turned off, against FFF fakes of APSDE-DATA.request and of the resolution of IEEE addresses of APS. The test passes frames to `zzb_zcl_data_ind()`, checks the frames caught by the fake, and defines a cluster of its own with `ZZB_ZCL_ATTR()` for the foundation commands. Fakes of the APS group functions keep the memberships in an array of the test. The records use NVS on the flash simulator; a reboot is `zzb_zcl_init()`, loading the settings and `zzb_zcl_start()`. The timers run in kernel time. Endpoints 1 to 4 serve the foundation commands, the clusters of milestones 12 and 13 and the scene attribute lists, endpoint 5 the client definitions, endpoints 10 and 11 Level Control, 20 and 21 the alarm table, 30 the power, temperature, ballast and sensing clusters with an Alarms server, 40 and 41 Color Control, 50 Window Covering, 60 the Thermostat with its weekly schedule, the Thermostat User Interface Configuration and the Thermostat client, 70 Fan Control, Pump Configuration and Control and the measurement clusters of milestone 15, 71 their clients, 80 the IAS Zone server, 81 the IAS Zone client, 85 and 86 IAS WD servers, 87 the IAS WD client, 90 and 91 the IAS ACE server of an IAS CIE and the IAS ACE client of a keypad, 100 and 101 Door Lock servers, 100 with an Alarms server, 102 the Door Lock client, 120 an On/Off Switch with the On/Off Switch Configuration server and the On/Off client, 121 a Simple Sensor with the Binary Input server, 122 a Meter Interface with the Metering server, and 140 and 141 the Power Profile server and the servers of the EN50523 appliance clusters of a White Goods and their clients on a Home Gateway. The `zcl_cmd_cb` of the test records the commands it receives. A second node, defined in `src/time.c`, has a Time server on endpoint 110 and Time clients on endpoints 111 and 112, so that the local time of the first node stays its UTC time plus the offset of its clock and the requests that a Time client sends at every start and every 60 s stay out of the other tests. A third node, defined in `src/poll_control.c`, has Poll Control servers on endpoints 160 and 161, so that its check-ins stay out of the other tests. A fourth node, defined in `src/ota.c`, has OTA Upgrade clients on endpoints 170 and 171, so that its queries stay out of the other tests. It writes the upload slot of the flash simulator through `flash_img` and the image manager of MCUboot without a bootloader, and the test checks the slot with `flash_area_read()` and `mcuboot_swap_type()`. The build of the suite runs `scripts/zigbee_ota.py` on `tests/common/mcuboot_image.bin`, a 32-octet MCUboot image header followed by a pattern, and a test checks the OTA file that the script writes. `zzb_ota_file()` of `tests/common/ota_file.cmake` runs the script and includes the OTA file in the test with `generate_inc_file_for_target()`.
- `tests/build/radio` is a build-only consumer application. It enables the module with NVS as the settings backend and, on nrf52840dk and ESP32-C6/H2, also the 802.15.4 driver in raw mode. A second scenario builds nrf52840dk with the custom-L2 backend. The application passes the `zephyr,ieee802154` chosen device, a device definition and a BDB callback to `zzb_node_init()`, and calls `zzb_bdb_start()` when BDB is built. The device has an Extended Color Light, a Color Dimmer Switch whose callback sends Toggle with `zzb_zcl_send_bound()` after steering, a Dimmable Ballast with Power Configuration, Device Temperature Configuration, Alarms and Illuminance Level Sensing, an Occupancy Sensor with Illuminance Measurement, a Window Covering, a Window Covering Controller, a Thermostat with Fan Control, Thermostat User Interface Configuration, Temperature and Relative Humidity Measurement, a Heating/Cooling Unit, a Pump with Alarms, Pressure and Flow Measurement, a Pump Controller, an IAS Zone with Poll Control, an IAS Warning Device with IAS Zone and IAS WD servers, an IAS ACE keypad with an IAS Zone server and an IAS ACE client, an IAS CIE with an IAS ACE server and Poll Control, IAS Zone and IAS WD clients, a Door Lock with Alarms, a Door Lock Controller, a Simple Sensor, a Smart Plug with Metering, Electrical Measurement and a Time client, a Meter Interface, a White Goods with Power Profile and the EN50523 appliance clusters, a Home Gateway with a Time server and the clients of the energy and appliance clusters, and an On/Off Switch with On/Off Switch Configuration, so every cluster of the stack is linked when ZCL is built; native_sim has no radio and builds no layer from the MAC up. A third scenario builds nrf52840dk, ESP32-C6 and ESP32-H2 with sysbuild and MCUboot and an OTA Upgrade client on the light endpoint; its `ota_cb` reboots, and its BDB callback confirms the image.
- `samples/` holds build-only consumer applications for nrf52840dk, ESP32-C6 and ESP32-H2. Each of the twelve family samples selects one device type of the DTL with a Kconfig choice, and its `sample.yaml` has one scenario per type; the Range Extender comes with the router. Four samples combine device types on the endpoints of one node: two relays with a switch, a sleepy multisensor, a door lock with a door contact and a power strip with metering. Battery-powered device types are sleepy end devices. `samples/common/` holds the code that all samples share: the button `sw0`, read through the gpio-keys driver of the Zephyr input subsystem (a short press starts steering off a network and the action of the device on a network, a hold of 3 s finding & binding, a hold of 10 s `zzb_bdb_reset()`), `led0` when the board has it, and the BDB callback, which starts steering off a network. Sensors produce synthetic values on timers. `samples/ota` is an On/Off Light with an OTA Upgrade client, built with sysbuild and MCUboot: CurrentFileVersion is APPVERSION of its `VERSION` file, the manufacturer code and the image type are Kconfig options, `ota_cb` reboots, and the BDB callback confirms the image at ZZB_BDB_EVENT_INIT with ZZB_BDB_SUCCESS. Its README describes the OTA file and the configuration of Home Assistant. The scenarios have the integration platform nrf52840dk, and CI builds them with `--integration` only for that board.
- CI runs on Woodpecker (`.woodpecker/build.yaml`): twister runs the tests on `native_sim` and `native_sim/native/64` and only builds the hardware targets. The step `docs` builds the HTML documentation of `include/zzb` with `doxygen doc/Doxyfile`, which fails on any warning, a public symbol without a comment included. A comment above a run of defines or struct fields applies to each of them as a Doxygen member group, `/** ... @{ */` up to `/** @} */`.
- `tests/common/scripted_tc.c` plays the trust center side of join sequences captured with a sniffer from EZSP and Z-Stack coordinators on a test network with a known network key.
- Reboot and power-loss tests run on NVS over the flash simulator, kept in RAM with `--flash_in_ram` so that every run starts with erased flash; the node is destroyed and re-initialized. In the NWK tests a reboot is a second `zzb_nwk_init()` followed by loading the settings.
- `tests/unit/nwk/src/poll.c` tests the poll scheduler against the MLME-POLL fake.
- `tests/integration/sleepy` asserts that a sleepy node on its network with nothing pending has only the poll timer, the keepalive, the timer of Poll Control and the timer of the OTA Upgrade client armed, and its receiver off.
- `tests/integration/nwk_data` runs two nodes on fake radios with the custom-L2 backend. Node 1 is a child of node 0; the test sets up both with NLME-SET and the neighbor table functions and, with APS turned off, sends APS frames built by hand.
- `tests/integration/nwk_join` runs one node on fake radio 1 against a parent scripted in `tests/common/parent.c` on fake radio 0. The script answers inside the `tx()` of the child: a beacon to a beacon request, an association response to the data request, the End Device Timeout Response and rejoin responses, and it secures and checks frames with the `sec/` functions. Frames it sends on its own, such as a leave request, are built on the stack thread with `zzb_call_sync()`, since a PSA key ID is valid only there. The parent also plays the trust center: after the association it sends the network key in a Transport Key secured with the key-transport key of the preconfigured key, and the APS suite runs the trust center link key exchange, acknowledgements in both directions, a retransmission, a duplicate and a network key switch. ZDO is turned off, and the APS callbacks of the test set apsTrustCenterAddress, the network key, the active key and the link key, as ZDO and BDB do. Every test starts with NLME-RESET, `zzb_aps_reset()` and a join, because ztest orders tests by name. All secured frames use the keys of `ci/check-frame-log.sh`; frames with a wrong MIC stay in the unit tests. For a child whose association or rejoin request has RxOnWhenIdle 0, the parent queues every frame to the child except beacons and the association response, and sends the head of the queue inside the `tx()` of a data request, with frame pending in the ACK, and in the frame when more frames wait; a frame older than 7.68 s is dropped.
- `tests/integration/bdb` runs the whole stack on fake radio 1 against the same parent, which also answers Node_Desc_req with a stack compliance revision set by the test, IEEE_addr_req, Simple_Desc_req with its endpoint 1 (Basic, Identify and On/Off servers) and Identify Query, logs the ZDO frames of the node, sends Mgmt_Leave_req, Bind_req and group-addressed frames to 0xfffd, and changes its channel. In this test the parent sends the network key itself, from a work item on the stack queue 130 ms after the association response or an unsecured rejoin response: the fake radio filters by the addresses a radio has when the frame is sent, and the node has no short address before it processes the association response. The scenarios are steering with and without the trust center link key exchange, a reboot with the rejoin of BDB 7.1, Mgmt_Leave_req with the reset, a parent that moves to another channel, ZCL (the parent reads Basic attributes and toggles On/Off), reporting (Bind_req and Configure Reporting, then a report after Toggle), Level Control (Bind_req and Configure Reporting of CurrentLevel, Move to Level with On/Off as ZHA sends it, a report of the end value), a group (Add Group, then a group-addressed On), the enrollment of an IAS Zone on the light endpoint (the parent writes IAS_CIE_Address with its IEEE address, the node sends Zone Enroll Request to the trust center address, the parent answers with Zone Enroll Response, and a change of ZoneStatus sends Zone Status Change Notification), a Door Lock on the light endpoint (Bind_req, then Lock Door without a code, as ZHA sends it, gets Lock Door Response SUCCESS and an Operation Event Notification to the binding), the Time client on the light endpoint (60 s after the reset of the node it reads the time from endpoint 1 of the parent, which answers as zigpy does, and the clock of the node takes it), steering with the install code key, and finding & binding as target and as initiator, after which the switch endpoint toggles the light with `zzb_zcl_send_bound()`. The node has an install code in every scenario; the parent secures the network key with the install code key in one scenario and with "ZigBeeAlliance09" in the others, so both preconfigured keys are tried. The parent sends and logs ZCL frames as bytes, since `nwk_join` builds it without ZCL. A reboot initializes NWK, APS, ZDO, ZCL and BDB again and loads the settings on the stack thread.
- `tests/integration/sleepy` runs the whole stack on a sleepy node against the same parent: steering with the network key, the trust center link key exchange and the End Device Timeout Response fetched by polls, the poll rates and the frame pending chain, the rejoin after a reboot and after a parent link failure, the reset after Mgmt_Leave_req, the Poll Control server with a check-in answered as zigpy does, and an OTA download of the file that `scripts/zigbee_ota.py` writes: the parent answers as zigpy does, with a unicast Image Notify, the Query Next Image Response, Image Block Responses and an Upgrade End Response with UpgradeTime 0, after which MCUboot has a test upgrade pending and `ota_cb` has been called. The parent keeps nwkParentInformation with the MAC data poll keepalive, so the keepalive of 256 minutes stays out of the poll counts; the trust center rejoin is tested in `tests/unit/bdb`.
- text2pcap and tshark decode the frame log of the medium and check MICs, as a decoder independent of the stack. The CI step `frame-log` runs after `native_sim` in the `antrea/tshark` image (Ubuntu 24.04, tshark 4.2.2), pinned by digest. `ci/check-frame-log.sh` takes the hexdumps of the medium from `handler.log`, builds a pcap with `text2pcap -l 230` (IEEE 802.15.4 without FCS) and decodes it with the test network keys and link keys; tshark derives the key-transport and key-load keys itself. The step fails if the log holds no frame with security, or a frame with NWK or APS security has fewer decrypted keys (`zbee.sec.key`) than auxiliary headers (`zbee.sec.key_id`). An APS ACK with APS security may have one key fewer, since tshark does not decrypt its empty payload.
- Hardware interoperability: joining ZHA with EZSP and Z-Stack coordinators from milestone 11; current measurement once the stack runs with `CONFIG_PM` (section 17). Milestone 11 runs Home Assistant with ZHA in Docker on the host, with the CC2531 as Z-Stack coordinator. Milestone 19 installs an image from ZHA on an ESP32-H2 with MCUboot.

## 21. Milestones

Each milestone is one feature branch with its tests. Milestones 14 to 17 cover the device types of the DTL, including the types of its Annexes C to H that cannot be certified.

1. Skeleton: `west.yml`, module files, Kconfig, CMake, CI with twister on native_sim and build-only nrf52840dk and ESP32-C6/H2 targets (`west blobs fetch hal_espressif`).
2. Common: buffer pool and metadata, stack work queue, request marshaling, timers, `zzb_node_init()`, EUI-64 provider.
3. Security primitives: MMO, HMAC-MMO, CCM level 5 with the level-field rule, auxiliary header, install code.
4. MAC frame codec: data, beacon and command frames.
5. Radio port: fake radio and medium, raw and custom-L2 shims, TX modes and error mapping, frame pending from the ACK.
6. MAC device side: active scan, association with response polling, data poll, software retries, filters, binding of the radio port to the node, EUI-64 from the custom-L2 link address.
7. NWK frame codec (header, the 13 commands of R22 3.4, beacon payload) and security, PSA key cache, NIB, neighbor table, frame counter persistence, NLDE-DATA for end devices, MLME-SET of macPANId and macShortAddress, tshark check of the frame log in CI.
8. NWK end device: network discovery, join, secure rejoin, trust center rejoin, leave (with rejoin flag), NLME-RESET, End Device Timeout Request and keepalive, parent link failure, network status, address conflict, persistence of the NWK state; MLME-SET of the channel and MLME-GET of the coordinator address; channels and a scripted parent in the fake medium.
9. APS end device: APSDE, ACK, retries with a new frame counter, duplicate rejection, link-key security, Transport Key, Request Key, Verify Key, Confirm Key, Switch Key, key-pair table, persistence of the trust center address and link keys; NLME-SET of the active network key; NWK reserves the MAC header of its frames instead of the longest one; the frame counter moves to `common/`; the scripted parent of `nwk_join` plays the trust center.
10. ZDO server (Node_Desc, Power_Desc, Simple_Desc, Active_EP, Match_Desc, IEEE_addr/NWK_addr, Mgmt_Leave, Bind/Unbind with the binding table, Mgmt_Lqi, Mgmt_Bind, Mgmt_Permit_Joining, Mgmt_NWK_Update channel change), Node_Desc_req client with a transaction table, Device_annce; the device definition with endpoints; APSDE-DATA to bound destinations; NLME-GET for ZDO and NLME-SET of the channel. The Node Descriptor server mask reports stack compliance revision 22: the TCLK exchange sends Request Key only after Node_Desc_rsp from the trust center reports revision 21 or later (BDB 10.2.5).
11. BDB end device: the public BDB API with an event callback, initialization and boot rejoin (7.1), steering (8.1, 8.2) with Mgmt_Permit_Joining_req, TCLK exchange (10.2.5), resets (chapter 9) including the reset after Mgmt_Leave_req (9.4), the rejoin series with a growing pause after parent loss; the NLME and Confirm Key callbacks, Device_annce after a join, a rejoin and an address change; NLME-RESET deletes the NWK records; ZDO frames on air in an integration test. Interop: join ZHA (Home Assistant in Docker with the CC2531 as Z-Stack coordinator; EZSP when an adapter is available) and stay joined across reboot.
12. ZCL foundation (read, write, discover attributes, default response), attribute macros, Basic, Identify and On/Off servers with the attributes and commands that the DTL requires for the On/Off Light except Groups and Scenes; StartUpOnOff with persistence; the ZCL callbacks of the application; endpoints with cluster instances. Interop: ZHA creates the entity.
13. Attribute reporting with persistence; finding & binding as target and initiator (BDB 8.4, 8.5) with the Identify client, Simple_Desc_req and IEEE_addr_req in the ZDO client and nwkAddressMap; install codes (BDB 6.4, 10.1); Groups and Scenes servers with the APS group table, which the DTL requires for the On/Off Light, the global scene in the scene table, group frames over NWK broadcast and NWK multicast.
14. Lighting device types of the DTL (0x0100 to 0x010e) and lighting controllers (0x0800 to 0x0850): Level Control for Lighting, Color Control, Illuminance Measurement, Illuminance Level Sensing, Occupancy Sensing, Power Configuration, Device Temperature Configuration and Ballast Configuration servers; the Alarms server with the alarms of these clusters; the ZCL client API for commands to bound destinations, used by switches and controllers.
15. Closures (Window Covering 0x0202, Window Covering Controller 0x0203) and HVAC (0x0300 to 0x0308): Window Covering, Thermostat with the weekly schedule, Thermostat User Interface Configuration, Fan Control, Pump Configuration and Control, Temperature, Pressure (with the extended set), Flow and Relative Humidity Measurement servers, their clients and `zcl_cmd_cb` for the commands that the application carries out. Shade and Shade Controller are deprecated and excluded; pumps have no Level Control.
16. Intruder alarm systems (0x0400 to 0x0403): IAS Zone server with enrollment and test mode, IAS WD server with the warning duration, IAS ACE server and client, IAS Zone and IAS WD clients; the CIE logic is in the application, which answers commands with `zzb_zcl_cmd_respond()`; the enum16 and EUI64 attribute types; ZCL commands to an IEEE address.
17. Generic device types (0x0000 to 0x0053) and the Simple Sensor: On/Off Switch Configuration, Door Lock with PIN users, Binary Input, Time server and client with the clock of the node, Metering, Meter Identification, Electrical Measurement, Power Profile and the EN50523 appliance clusters, whose logic is in the application; the int24, int32, UTC, uint48 and uint56 types. The Range Extender comes with the router.
18. Sleepy end device: the sleepy flag of the node, the receiver off outside MLME procedures, the poll scheduler with fast poll holds, polls for the rejoin and End Device Timeout responses, the Poll Control server and client; the scripted parent queues frames for a sleepy child.
19. OTA Upgrade client (ZCL 11) with MCUboot: downloads from the trust center into the upload slot with `flash_img`, `boot_request_upgrade()` at the upgrade time and `ota_cb`; the `mcuboot` module; `scripts/zigbee_ota.py`. Interop: ZHA installs an image on an ESP32-H2 with MCUboot.
20. Router, part 1: MAC coordinator functions (beacons, association response, indirect queue, frame-pending table), stochastic address allocation, child aging and End Device Timeout server, permit join, Parent_annce; esp32 driver patch; frame loss in the fake medium; the Range Extender device type.
21. Router, part 2: route discovery, link status, broadcast transaction table with passive ACK, many-to-one routes, route record, source-route relay, APS Update Device and Tunnel.
22. Coordinator: network formation (energy scan, PAN ID selection), concentrator and source route table, network manager.
23. Trust center (BDB 10.3): key generation with `psa_generate_random()`, join and rejoin policy, network key update and switch, per-entry storage.
24. NCP; Green Power proxy basic.

## 22. Risks

| Risk | Mitigation |
|---|---|
| The stack passes its own tests but fails with real coordinators | ZHA interop from milestone 11; fixtures from sniffer captures; independent decoder in CI |
| esp32 driver: no frame-pending control, no custom-L2 branch, no CCA-only TX mode, `tx()` returns 0 after CCA failure and missing ACK, no PM support in v4.4.2, binary blobs | raw mode, software retries, a random wait before each transmission, zero-length frame fallback, ACK detected through `ieee802154_handle_ack()`, a PM lock of the stack while the radio works, the modem domain kept on by the application, upstream patch, blob fetch in CI |
| Frame counter or persistence loss: the device cannot rejoin, or reports stop | watermark key outside the factory-reset subtree; reboot and power-loss tests; one settings key per record |
| Router and coordinator scope | ZigBee PRO/2007 layer PICS (08-0006-05) as a per-milestone checklist; features outside the ZHA path deferred |
| Sleepy timing and current are not observable on native_sim | receiver model of the fake radio; deadline-only timers with an idle test; current measurement with `CONFIG_PM` |
| The flash operations of an OTA download run on the stack thread: the CPU of the nRF52840 halts during a page erase (about 85 ms), and ESP32 runs only IRAM code during SPI flash operations, so frames can be lost | one page erase per 4 KiB of image with `CONFIG_IMG_ERASE_PROGRESSIVELY`; MAC, APS and block retries |
| The CI image lacks the Python packages of imgtool, so the sysbuild scenario cannot sign | install the requirements of the mcuboot module in the hardware step |
| A new image boots but does not join its network | the application confirms the image after BDB initialization, and MCUboot reverts an unconfirmed image in the swap modes; the overwrite-only mode of ESP32 does not revert, so an image is tested before it is offered |

New dependencies accepted with this document: PSA Crypto (Zephyr mbedTLS / TF-PSA-Crypto), settings with NVS or ZMS, FFF, `net_buf`. The `antrea/tshark` container image is accepted for the frame log check (milestone 7). Milestone 19 adds the `mcuboot` module, the image manager of Zephyr (`flash_img` with `CONFIG_MCUBOOT_IMG_MANAGER`) and Python 3 with its standard library for `scripts/`. Doxygen, which the Zephyr CI image contains, builds the API documentation. Zephyr shell is confirmed at the milestone that introduces it.
