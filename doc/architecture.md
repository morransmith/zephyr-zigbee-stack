# zzb architecture

zzb is a Zigbee 3.0 stack for Zephyr: Zigbee PRO R22 (05-3474-22), Base Device Behavior (BDB) 3.0.1, Zigbee Cluster Library (ZCL) rev 8, Device Type Library rev 2. Clause numbers in this document refer to these revisions.

## 1. Scope

Roles, in implementation order: end device, sleepy end device, router, coordinator, trust center, network co-processor (NCP).

Interoperability targets: ZHA with EmberZNet (EZSP) and Z-Stack coordinators, and R23 trust centers. An R23 trust center accepts the Zigbee 3.0 join mechanism (R23.2 key negotiation method 0x00, BDB 3.1 5.6.5), so R23-only procedures are not required.

Excluded: R23 features (TLVs, dynamic link key, Security_Start, NWK commissioning, ZDO restricted mode), Sub-GHz, touchlink, Zigbee Direct. APS fragmentation and distributed security are added when a use case requires them; ZHA networks use centralized security.

## 2. Packaging and layout

The repository is a Zephyr module and a west T2 manifest repository, following zephyrproject-rtos/example-application. `west.yml` pins Zephyr v4.4.2. `zephyr/module.yml` sets `build.cmake: .` and `build.kconfig: Kconfig`.

```
west.yml  zephyr/module.yml  CMakeLists.txt  Kconfig  LICENSE  NOTICE
include/zzb/      zzb.h aps.h zdo.h bdb.h zcl.h zcl/<cluster>.h types.h
subsys/zzb/
  common/   buf.c queue.c timer.c eui64.c eui64_esp32.c node.c fc.c
  radio/    port.c raw.c custom_l2.c
  mac/      frame.c mac.c scan.c assoc.c poll.c tx.c
            coord/   beacon.c assoc_resp.c indirect.c fpb.c
  nwk/      nwk.c frame.c nib.c neighbor.c addr_map.c security.c join.c rejoin.c leave.c
            ed_timeout.c status.c
            router/  addr_alloc.c route.c link_status.c btt.c mtorr.c src_route.c
            coord/   formation.c concentrator.c nwk_manager.c
  sec/      ccm.c mmo.c hmac.c aux_hdr.c key_cache.c install_code.c
  aps/      aps.c frame.c apsde.c ack_dup.c keys.c security.c cmd.c binding.c group.c tunnel.c
  zdo/      zdo.c server.c desc.c mgmt.c client.c
  bdb/      init.c steering.c tclk.c reset.c fb.c formation.c
  zcl/      frame.c attr.c foundation.c report.c clusters/
  tc/       trust center
  storage/  settings.c
  shell/    debug shell
  ncp/      host interface
tests/common/                fake_radio.c scripted_tc.c fixtures/
tests/unit/<layer>/
tests/integration/<scenario>/
tests/glue/{raw,custom_l2}/
tests/build/radio/
samples/  on_off_light  contact_sensor
ci/       release.sh check-frame-log.sh
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

The device definition (endpoints, clusters, attribute storage) is passed to `zzb_node_init()`. The stack does not use global iterable sections.

`struct zzb_node_config` points to a `struct zzb_device`: the manufacturer code, the power source and the endpoints in ascending order of endpoint ID, each with the fields of its simple descriptor (profile, device ID, device version, input and output cluster IDs). The node keeps the pointer, so the definition must outlive the node. With `CONFIG_ZZB_ZDO`, `zzb_node_init()` returns `-EINVAL` without one. The application keeps every simple descriptor within apscMaxDescriptorSize (64 octets, 28 clusters); the stack does not check it. ZCL (milestone 12) replaces the cluster ID lists with cluster instances. `zzb_node_init()` loads the settings records of the node with `settings_load_subtree_direct("zzb/<n>")`, where n is the index of the node in the pool, and passes each key to its layer. The stack registers no settings handler, so a `settings_load()` call of the application does not reach a node before it exists.

The `net_buf` pool and the PSA key cache are shared by all nodes.

## 4. Execution model

- A dedicated `k_work_q`, shared by all nodes and started at boot, owns all stack state. Stack code needs no locks.
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
- Unit tests replace adjacent layers by link-time substitution with the FFF bundled with Zephyr (`zephyr/fff.h`): a test links the sources of the layer under test and defines fakes with the names of the neighbor layer's functions. Zephyr links libzzb with `--whole-archive`, so each layer has a Kconfig option that removes it from the build. `CONFIG_ZZB_MAC` defaults to y when a radio backend is enabled; the glue tests turn it off because they fake `zzb_radio_rx()` and `zzb_radio_tx_done()`. `CONFIG_ZZB_NWK` defaults to the value of `CONFIG_ZZB_MAC`; the MAC tests turn it off, and the NWK tests turn it on with the MAC off. `CONFIG_ZZB_APS` defaults to the value of `CONFIG_ZZB_NWK`; the NWK tests turn it off, and the APS tests turn it on with NWK off. `CONFIG_ZZB_ZDO` defaults to the value of `CONFIG_ZZB_APS`; the APS tests turn it off, and the ZDO tests turn it on with APS off. BDB and ZCL get their options when they are added.

## 6. Tables

| Layer | Tables |
|---|---|
| MAC | PIB, TX queue, indirect queue, mirror of the driver frame-pending table |
| NWK | neighbor table (parent and children as entries with relationship, timeout, keepalive, incoming frame counter), network descriptors of the last discovery, `nwkAddressMap`, route table, route discovery table, broadcast transaction table, route record table, network keys (two), group ID table |
| APS | device key-pair table (key attributes, link key type, incoming frame counter), trust center IEEE address, duplicate rejection table, transmission table, binding table; group table (when a use case requires it) |
| ZDO | descriptors generated from the device definition, transaction table |
| ZCL | reporting slots |

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

In raw mode no `net_if` exists, and the drivers pass a NULL interface to `net_recv_data()` and `ieee802154_handle_ack()`. The raw backend therefore serves one port. It implements `net_recv_data()`, which the driver calls for each received frame; `ieee802154_init()` is an empty macro. The custom-L2 backend implements an empty `ieee802154_init()`, finds the interface of the device with `net_if_lookup_by_dev()` and keeps the port in the L2 data of the interface. It receives frames through the `recv` function of its `NET_L2_INIT()` entry. Its `send` returns `-ENOTSUP`, and it does not start the radio; the MAC does.

| Item | Behavior |
|---|---|
| TX mode | `IEEE802154_TX_MODE_CSMA_CA` when the driver reports `IEEE802154_HW_CSMA`; otherwise unslotted CSMA-CA with `cca()` and `k_busy_wait()` (802.15.4-2011 5.1.1.4; macMinBE 3, macMaxBE 5, macMaxCSMABackoffs 4, unit backoff period 320 us), then `IEEE802154_TX_MODE_DIRECT` |
| Status | `enum zzb_mac_status` with the 802.15.4-2006 values. NO_ACK (0xe9) for `-ENOMSG`, and for a frame with the ACK request bit when `ieee802154_handle_ack()` was not called during `tx()`, also if `tx()` returned 0. CHANNEL_ACCESS_FAILURE (0xe1) for every other error. SUCCESS (0x00) otherwise |
| Retries | in software; neither nrf5 nor esp32 reports `IEEE802154_HW_RETRANSMISSION` |
| Frame pending after a poll | parsed from the ACK frame passed to `ieee802154_handle_ack()`; drivers do not set `ack_fpb` on ACK packets |
| Filters | the MAC programs the IEEE address, PAN ID and short address at init and on every change; neither nrf5 nor esp32 programs the IEEE address in raw mode |
| Channel | `set_channel()` followed by `start()`, since the esp32 driver applies a new channel only when it next enters TX or RX |
| Trailing bytes | `CONFIG_IEEE802154_L2_PKT_INCL_FCS` defaults to y in raw mode. The port then drops the last two bytes of a received frame: nrf5 puts the FCS or the LQI there, esp32 the RSSI and the LQI |

The esp32 driver returns 0 from `tx()` after a CCA failure and after a missing ACK, because it discards the error passed by the HAL. A unicast frame then reports NO_ACK by the rule above, and a broadcast frame lost to a CCA failure reports SUCCESS.

Frame pending on the parent side:

- nrf5: `IEEE802154_CONFIG_AUTO_ACK_FPB` with `IEEE802154_FPB_ADDR_MATCH_ZIGBEE` and the `IEEE802154_CONFIG_ACK_FPB` table. In Zigbee mode the table lists short addresses without pending data; data requests from extended addresses always get the bit set.
- esp32: the driver returns `-ENOTSUP` for both settings and sets frame pending on every ACK to a data request. When a data request arrives with `ack_fpb` set and nothing is queued for the child, the MAC sends a zero-length data frame without ACK request, so the child can sleep (802.15.4-2011 5.1.6.3). The same rule covers table races on nrf5. The upstream fix maps both settings to `esp_ieee802154_set_pending_mode(ESP_IEEE802154_AUTO_PENDING_ZIGBEE)` and `esp_ieee802154_add_pending_addr()`, adds the custom-L2 branch to the driver, and returns `-EBUSY` and `-ENOMSG` from `tx()`; it is part of milestone 15. Software emulation of auto-ACK is not possible within the ACK turnaround time.

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
- The receiver stays on between polls until milestone 14.
- MLME-SET of macPANId, macShortAddress and phyCurrentChannel (`zzb_mlme_set_pan_id()`, `zzb_mlme_set_short_addr()`, `zzb_mlme_set_channel()`) writes the PIB and the radio. `zzb_mlme_set_channel()` returns `-EBUSY` during an MLME procedure, since the end of a scan restores the channel. MLME-GET of macCoordExtendedAddress (`zzb_mlme_get_coord_ext_addr()`) gives NWK the IEEE address of the parent after an association. MLME-RESET is not implemented (section 10, Leave and reset).

| Time | Definition | Value at 2.4 GHz |
|---|---|---|
| Scan dwell | aBaseSuperframeDuration (960 symbols) * (2^n + 1) | 138.24 ms for n = 3, the value R22 D.7 recommends |
| macResponseWaitTime | 32 * aBaseSuperframeDuration | 491.52 ms |
| macMaxFrameTotalWaitTime | 802.15.4 Table 52 formula with macMinBE 5, macMaxBE 8 and macMaxCSMABackoffs 4 (R22 D.6), since the frame comes from the CSMA-CA of the parent | 9846 symbols, 157.54 ms |

The 2011 text of the macMaxFrameTotalWaitTime formula has 2^macMinBE in its second term. The value above uses 2^macMaxBE, because the backoffs after the first m use BE = macMaxBE.

## 10. NWK

`CONFIG_ZZB_NWK` builds `nwk/`, `common/fc.c` and `storage/settings.c`. It selects `CONFIG_SETTINGS`; the application enables the settings backend (section 14). Milestone 8 implements the end device side of join, rejoin, leave and the End Device Timeout; the parent side is part of the router (milestone 15).

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
- A neighbor table entry holds the extended address, network address, device type, relationship, LQI and two incoming frame counters, one per key slot (R22 4.3.1.2 bounds the counters by the number of keys times the number of neighbors). The discovery fields of R22 Table 3-64 are the parsed beacon payload (`struct zzb_nwk_beacon`), the PAN ID, the channel, permit joining and potential parent. The device type is coordinator when the beacon has the PAN Coordinator bit of the Superframe Specification set, otherwise router. RxOnWhenIdle, timeout and keepalive describe children and come with the router (milestone 15). The table has `CONFIG_ZZB_NEIGHBOR_COUNT` entries (default 16); add returns NULL when it is full.
- `zzb_nwk_neighbor_find_short()` skips entries with relationship none, which come from beacons, so the next hop of NLDE-DATA and the LQI update use only the parent and children.

### Frame security

Two functions secure and unsecure in place a buffer whose data starts at the NWK header (R22 4.3.1).

- Outgoing: the active key (none: `-ENOENT`), the next outgoing frame counter (section 13), an auxiliary header with key identifier 1, the extended nonce and the EUI-64 of the node, inserted after the NWK header by moving the header into the headroom, then CCM and the MIC at the tail.
- Incoming: the auxiliary header must have key identifier 1 and the extended nonce, otherwise `-EINVAL`. A frame counter of 2^32-1 or below the stored counter gives `-EALREADY`. The key slot is found by the key sequence number and the neighbor by the source address of the auxiliary header; a missing slot or neighbor gives `-ENOENT`. A MIC mismatch gives `-EBADMSG`. After verification the counter of the slot is set to the received counter + 1.
- During a rejoin the candidate parent may be known only from its beacon, with extended address 0. A frame whose MAC source is the short address of that candidate is then verified against the candidate entry, which takes the source address of the auxiliary header after the MIC check (R22 3.6.1.9.2). Outside a rejoin, frames from devices without a neighbor entry are dropped; an end device receives a broadcast relayed by another router again from its parent.
- A frame secured with the key of the slot written last, when that slot is not active, makes that key active (R22 4.3.1.2 step 7) and saves the key record. A frame secured with the older key does not switch back.
- R22 4.3.1.2 step 8 (unauthenticated child) is part of the router (milestone 15).

### Data service

- `zzb_nlde_data_req(node, buf, dst_addr, radius, secure)` takes the NSDU after `ZZB_NLDE_HEADROOM` bytes of headroom and the NSDU handle in the frame metadata. It returns `-ENOTCONN` while the node has no network address or no parent, `-EBUSY` while an NLME procedure runs, and `-EINVAL` for a handle from 0xf0, which NWK command frames use. The header has frame type data, discover route 1 for unicast and 0 for broadcast, end device initiator set on an end device whose nwkParentInformation is not 0 (R22 3.3.1.1.9), and no IEEE address fields. Radius 0 selects the default radius.
- The MAC destination is the neighbor whose network address is the NWK destination, otherwise the parent. Unicast frames request a MAC ACK. A broadcast from an end device is a MAC unicast to the parent without ACK request (R22 3.6.5).
- Security and MCPS-DATA.request errors are returned by the request, which then leaves the buffer with the caller. MCPS-DATA.confirm produces `zzb_nlde_data_conf(node, handle, status)` with the MAC status.
- Received data frames addressed to the node, to 0xffff or to 0xfffd (the receiver stays on until milestone 14) produce `zzb_nlde_data_ind(node, buf, dst_addr, src_addr, security_use)` with the NSDU in the buffer and the LQI in the metadata. Frames with the network address of the node as source are dropped, and an end device drops frames with the end device initiator bit (R22 3.6.2.2). Command frames go to the command handlers below. Every received frame updates the LQI of the neighbor whose network address matches the MAC source address.
- There is no broadcast transaction table until milestone 16. A repeated secured frame from the parent is rejected by the incoming frame counter check.
- `zzb_nlde_data_conf()` and `zzb_nlde_data_ind()` are defined by APS. Tests that turn APS off define them.

### Command frames

- NWK command frames are sent through the same function as NLDE-DATA frames. Their MSDU handle is 0xf0 | command identifier; `zzb_mcps_data_conf()` passes other handles to `zzb_nlde_data_conf()`. Command frames have discover route 0 (R22 3.3.1.1.3), the source IEEE address, and the destination IEEE address when it is known.
- An end device handles the rejoin response, leave, End Device Timeout Response and network status commands and drops the others. The network status reply 0x13 to an unknown unicast command (R22 3.6.13) comes with the router (milestone 15); R21 devices do not send it either.

### Unsecured frames

R22 3.6.2.2:

- After a successful association join or trust center rejoin the network key is pending until NLME-SET of a network key. While it is pending, unsecured data frames reach APS, which accepts only Transport Key among them (section 11). NWK cannot check the APS command identifier, because APS encrypts it.
- An unsecured rejoin response is accepted only by a trust center rejoin waiting for it. All other unsecured frames are dropped.

### NLME procedures

- Network discovery, join, rejoin and leave exclude each other; a second request returns `-EBUSY`. A request that fails before anything is sent returns an error and produces no confirm; otherwise exactly one confirm follows. The procedure state is cleared before the confirm, so ZDO can issue the next request from the callback.
- Confirms carry a `uint8_t` status: a value of `enum zzb_nwk_status` (R22 Table 3-73) or the MAC status that R22 passes through; the two ranges do not overlap. `enum zzb_nwk_status_code` holds the codes of R22 Table 3-51, used by network status commands and NLME-NWK-STATUS.indication.
- The `zzb_nlme_*_conf()` and `zzb_nlme_*_ind()` callbacks are defined by BDB (milestone 11), part of the ZDO application object (section 5); until then the tests and `tests/build/radio` define them.
- One `k_work_delayable` serves the rejoin response wait, a second one the End Device Timeout Request and the keepalive. Both are rescheduled with `k_work_reschedule_for_queue()`, since `zzb_timer_arm()` only moves a deadline earlier. `zzb_nwk_init()` cancels them first; leave and reset clear the NWK fields one by one and never assign over a `k_work`.

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

- `zzb_nlme_join_req(node, ext_pan_id, method, channels, duration, capability, secure)` takes method `ZZB_NWK_JOIN_ASSOC` (0x00) or `ZZB_NWK_JOIN_REJOIN` (0x02). It returns `-EINVAL` for extended PAN ID 0, and `-ENOTSUP` for orphaning, a channel change, the router device type (until milestone 15) and RxOnWhenIdle 0 (until milestone 14, since the Transport Key and the rejoin response then need polling). Every join first sets nwkParentInformation to 0 (R22 3.2.2.13.3) and stops the keepalive.
- Association (R22 3.6.1.4.1): channels and duration are ignored, since the channel comes from the neighbor entry and BDB 8.2 passes 0; R22 3.2.2.13.3 requires a single channel. The request returns `-EALREADY` on a node on a network (R22 INVALID_REQUEST) and `-ENETUNREACH` without a candidate (R22 NOT_PERMITTED). MLME-ASSOCIATE goes to the channel, PAN ID and short address of the candidate. After a failure the next candidate is tried; without one the confirm carries the last MAC status. On success the candidate becomes the parent with the extended address from MLME-GET of macCoordExtendedAddress, the other entries are removed, the NIB takes the PAN ID and address that the MAC already holds, and the key becomes pending. Nothing is saved until the key arrives.
- Rejoin (R22 3.6.1.4.2): an active scan of channels, or of the channel of the network when channels is 0 (BDB 7.1 passes 0). Only beacons with the requested extended PAN ID are used. The NIB PAN ID and the MAC channel are set to those of the candidate. The source address is nwkNetworkAddress; when it is not valid, a random address from 0x0001 to 0xfff7 is written to the NIB and the MAC, since the rejoin response is addressed to it. The rejoin request has radius 1, the source IEEE address, the destination IEEE address when the candidate's is known, NWK security when secure, and the capability with allocate address 0 (stochastic addressing).
- After the confirm of the rejoin request with SUCCESS, NWK waits macResponseWaitTime (491.52 ms) for the rejoin response; a response that arrives before the confirm is accepted too. The response must come from the candidate, with the EUI-64 of the node as destination IEEE address, the IEEE address of the candidate as source when it is known, status SUCCESS and an address below 0xfff8. While a rejoin runs, all other frames are dropped.
- Without any beacon the confirm is NO_NETWORKS, after the last candidate NOT_PERMITTED, and the PAN ID, channel and address of the node before the rejoin are restored. On success the candidate becomes the parent with the IEEE address from the response header, the other entries are removed, and the node takes the address in the response. A trust center rejoin leaves the key pending; a secure rejoin saves the NWK records and starts the End Device Timeout Request.

### End device timeout and keepalive

- The End Device Timeout Request (R22 3.6.10.2) goes to the parent with the timeout `CONFIG_ZZB_NWK_ED_TIMEOUT` (default 8, 256 minutes, the value of nwkEndDeviceTimeoutDefault), configuration 0, radius 1, both IEEE addresses and NWK security. It is started on the keepalive timer with delay 0; a request that cannot be sent (no buffer, frame counter busy) is retried after 1 s.
- A response is accepted only from the parent, checked by the MAC source and the source IEEE address. SUCCESS sets nwkParentInformation and starts the keepalive with a period of timeout / 3 (R22 3.6.10.3). With bit 0 of nwkParentInformation the keepalive is MLME-POLL to the parent; otherwise, with bit 1, it is an End Device Timeout Request answered within macResponseWaitTime. A missing or unsuccessful answer produces NLME-NWK-STATUS.indication with parent link failure (0x09) and stops the keepalive.
- A keepalive that cannot start (MLME-POLL `-EBUSY` or `-ENOBUFS`, an NLME procedure running) is retried after 1 s. After loading the NWK records with nwkParentInformation not 0 the keepalive starts at once (R22 3.6.10.8).

### Parent link failure

While the node has a parent and no NLME procedure runs, NO_ACK in MCPS-DATA.confirm and MLME-POLL.confirm counts a failure, and SUCCESS and NO_DATA reset the count. CHANNEL_ACCESS_FAILURE means a busy channel and changes nothing. At `CONFIG_ZZB_NWK_PARENT_FAILURES` (default 3) NWK issues NLME-NWK-STATUS.indication with parent link failure and the address of the parent and resets the count; the keepalive failure also resets it. BDB rejoins (milestone 11).

### Leave and reset

- `zzb_nlme_leave_req(node, rejoin)` is the end device form of NLME-LEAVE.request: the node removes itself and has no children. It returns `-ENOTCONN` when the node is not on a network or has no parent, and `-ENOENT` without a network key. The leave command has destination 0xfffd, radius 1, the source IEEE address and NWK security. It goes to the parent as MAC unicast with ACK request (R22 3.4.4.1), so NLME-LEAVE.confirm reports delivery; the rule for broadcasts from end devices in Data service does not apply. After its confirm the local leave runs, then `zzb_nlme_leave_conf(node, status)`.
- Local leave without rejoin (R22 3.6.1.10.4) clears the NIB, the neighbor table, the network descriptors, the pending key state, the failure count, the keepalive and the rejoin candidate, sets macPANId and macShortAddress to 0xffff through MLME-SET, and deletes the NWK records. The outgoing frame counter and nwkSequenceNumber are kept. MLME-RESET is not used: aborting an MLME procedure whose command frame is in the radio is unsafe, and MLME-SET resets the attributes the MAC holds. With rejoin nothing changes, including nwkExtendedPANID; R22 3.6.1.10.1 clears it, but the rejoin that follows needs it.
- `zzb_nlme_reset_req(node)` is NLME-RESET.request with WarmStart FALSE: the same clearing without a frame and without deleting the records. It returns `-EBUSY` while an NLME procedure runs. BDB uses it when the network key does not arrive (BDB 8.2 step 10), since a leave needs the key.
- A received leave request (request 1) is processed only as a unicast from the parent, checked by the MAC source, while no NLME procedure runs. The node sends the leave command with the rejoin flag of the request, performs the local leave and issues `zzb_nlme_leave_ind(node, 0, rejoin)`.
- A received leave indication (request 0) from the parent with remove children makes the node leave in the same way. Without remove children the parent entry is removed, and `zzb_nlme_leave_ind(node, parent IEEE address, rejoin)` and NLME-NWK-STATUS.indication with parent link failure follow, so that BDB rejoins another parent. The R22 text after 3.6.1.10.3.1 can also be read as removing the end device in both cases (R23 has the same wording); with that reading, removing a router from ZHA would take its end device children off the network. A leave indication from another neighbor removes its entry and issues `zzb_nlme_leave_ind(node, its IEEE address, rejoin)`.

### Network status and address conflicts

- A received network status command produces `zzb_nlme_nwk_status_ind(node, code, addr)` with the address in the payload, or the NWK source address when the payload has none.
- On a node on a network with no NLME procedure running, a frame to the address of the node with a destination IEEE address other than its EUI-64 (R22 3.6.1.9.2, checked after NWK security) is dropped. The node broadcasts a network status command with address conflict (0x0d) and its address to 0xfffd, with the source IEEE address and NWK security, as MAC unicast to the parent without ACK request, and issues NLME-NWK-STATUS.indication with the same code and address; BDB rejoins. The random jitter of R22 3.6.1.9.3 is not applied, because the frame of an end device goes to its parent, which applies the jitter when it relays the broadcast.
- An unsolicited rejoin response from the parent with a new address sets the address, saves the NWK records and produces NLME-NWK-STATUS.indication with network address update (0x10) and the new address (R22 3.6.1.9.3).

## 11. APS

`CONFIG_ZZB_APS` builds `aps/`, `common/fc.c` and `storage/settings.c` and selects `CONFIG_SETTINGS`. Milestone 9 implements the end device side: data service, acknowledgements, retransmissions, duplicate rejection, link-key security and the key commands a joining device exchanges with the trust center.

- The binding table serves ZDO Bind/Unbind and APSDE-DATA with binding (milestone 10). The group table is added when a use case requires it; until then received group-addressed frames are dropped. Tunnel, Update Device and Remove Device are part of the router (milestone 16).
- Application link keys are excluded: Request Key asks only for the trust center link key, and a received application link key is dropped.
- The primitives are internal and run on the stack thread, as in NWK. The `zzb_apsde_data_conf()`, `zzb_apsde_data_ind()`, `zzb_apsme_transport_key_ind()` and `zzb_apsme_switch_key_ind()` callbacks are defined by ZDO (section 12). `zzb_apsme_confirm_key_ind()` is defined by BDB (milestone 11); until then the tests and `tests/build/radio` define it. The public tier 1 API (section 17) is not part of milestone 10.

### Frame codec

`aps/frame.c` is a byte codec in the style of `nwk/frame.c`.

- The header struct holds the frame type, delivery mode, ack format, security, ack request and extended header flags, the destination endpoint, group address, cluster, profile, source endpoint and APS counter. The parser rejects inter-PAN frames and fragmented frames (fragmentation sub-field not 0) with `-ENOTSUP` and nonzero reserved fields with `-EINVAL`.
- Commands: Transport Key with its three key descriptors, Request Key, Switch Key, Verify Key and Confirm Key. Other command identifiers give `-ENOTSUP`.

### Data service

- `zzb_apsde_data_req(node, buf, req)` takes the ASDU with the headroom of section 7, the ASDU handle in the frame metadata, and `struct zzb_apsde_req` with the destination address mode and address, destination endpoint, profile, cluster, source endpoint, TxOptions and radius.
- Destination address mode 0x01 sends a group-addressed frame to 0xfffd. Mode 0x02 sends to a network address; a broadcast address gives delivery mode broadcast. Mode 0x03 resolves the IEEE address to 0x0000 when it is apsTrustCenterAddress, otherwise to a neighbor with a relationship, otherwise the request returns `-ENOENT`. Mode 0x00 sends to the bound destinations (Binding table below).
- TxOptions uses the R22 bits: security 0x01, acknowledged transmission 0x04, extended nonce 0x10. Use NWK key (0x02) has no effect, since every frame has NWK security. Fragmentation (0x08) returns `-ENOTSUP`, and an acknowledged broadcast or group frame `-EINVAL`.
- A request that fails before anything is sent returns an error and produces no confirm; otherwise exactly one `zzb_apsde_data_conf(node, handle, status)` follows. The status is SUCCESS, NO_ACK (0xa7) or the status of NLDE-DATA.confirm, which R22 2.2.4.1.1 passes through.
- Each request takes an entry of the transmission table (`CONFIG_ZZB_APS_TX_COUNT`, default 4); a full table returns `-ENOBUFS`. The NSDU handle is the entry index. Frames APS sends on its own, acknowledgements and commands, use handle 0xef, and their NLDE-DATA.confirm is ignored.
- An unacknowledged frame is secured in place and passed to NLDE-DATA, and its NLDE-DATA.confirm produces the APSDE-DATA.confirm.
- An acknowledged frame keeps its plaintext in the entry. Every attempt copies it into a new buffer, secures the copy with the next APS frame counter and passes it to NLDE-DATA (section 7). All attempts have the same APS counter.
- The wait for the acknowledgement, apscAckWaitDuration = 0.05 * 2 * nwkcMaxDepth + 0.1 = 1.6 s, starts at NLDE-DATA.confirm with SUCCESS. An acknowledgement that arrives before the confirm is accepted.
- An attempt fails at an NLDE-DATA.confirm other than SUCCESS, at the end of the wait, or when its copy cannot be sent (no buffer, an NLDE-DATA error). The next attempt follows at once after a failed confirm and after apscAckWaitDuration after a send error. After apscMaxFrameRetries (3) retries the confirm carries the status of the last failed NLDE-DATA.confirm, or NO_ACK when there was none.
- The entry is freed before the confirm, so the upper layer can send from the callback. An entry whose acknowledgement arrives before its NLDE-DATA.confirm produces the confirm at the acknowledgement and is freed at the NLDE-DATA.confirm, so each entry has at most one NSDU in flight.
- One `k_work_delayable` serves the table, rescheduled for the earliest deadline with `k_work_reschedule_for_queue()`.
- An acknowledgement matches an entry by APS counter, cluster, source endpoint equal to the destination endpoint of the frame, and network source equal to its network destination (R22 2.2.8.4.4). APS sends commands without acknowledgement request, so received command acknowledgements are dropped.
- The APS counter starts at a random value, so the duplicate rejection table of a peer does not drop the first frames after a reboot.

### Reception

`zzb_nlde_data_ind()` processes a frame in this order:

1. The header is parsed. Group-addressed, inter-PAN and fragmented frames are dropped.
2. A frame without NWK security reaches APS only while the network key is pending (section 10). Among such frames only a Transport Key with APS security is accepted.
3. APS security is removed (Frame security below).
4. An acknowledgement goes to the transmission table and does not enter the duplicate rejection table.
5. A command must pass the checks of Commands below. A frame that fails them is dropped without acknowledgement and duplicate entry.
6. A duplicate is acknowledged again when it requests an acknowledgement, and it is not delivered.
7. The acknowledgement of a frame that requests one and was a NWK unicast to the node is built: ack format 1 for a command; for a data frame the endpoints swapped and the cluster, profile and counter copied. It has APS security when the frame had it, with the key that removed it as data key, also when that key is unverified.
8. The frame enters the duplicate rejection table and is delivered with `zzb_apsde_data_ind(node, buf, ind)` or to the command handler.
9. The acknowledgement goes to NLDE-DATA with NWK security. An error, such as `-ENOENT` without a network key, drops it.

The acknowledgement follows the delivery because an R23 trust center that is the parent requests an acknowledgement of the network key Transport Key (R23 4.4.11), and after the delivery ZDO has set the key that secures it. The acknowledgement of a trust center link key Transport Key is secured with the old link key, which decrypted the frame, although BDB replaces that key during the delivery. R22 does not say whether acknowledgements carry APS security; milestone 11 compares this with a sniffer capture.

- The duplicate rejection table (`CONFIG_ZZB_APS_DUP_COUNT`, default 8) holds the network source, the APS counter and an expiry time. R22 does not define the lifetime; an entry lives apscAckWaitDuration * (apscMaxFrameRetries + 1) = 6.4 s, the time a sender with the same constants retransmits. Expiry is checked against `k_uptime_get()` at lookup, so no timer runs. A full table replaces the entry that expires first.
- `struct zzb_apsde_ind` holds the network destination address and the destination endpoint of the frame, the network source address, the source endpoint, profile, cluster and the security status (NWK key, link key). Endpoint 0xff is passed up, and ZDO dispatches it. The LQI is in the frame metadata. The source stays a network address until nwkAddressMap exists.

### AIB and key-pair table

- apsTrustCenterAddress starts as 0xffffffffffffffff, unknown (BDB 10.2.2). Distributed security is excluded (section 1), so the value does not mean a distributed network.
- The preconfigured key is the default global trust center link key "ZigBeeAlliance09". BDB adds the install code key (milestone 11).
- A key-pair entry holds the device address, the key attributes (provisional, unverified, verified), the link key type (unique, global), the link key and the incoming frame counter. The table has `CONFIG_ZZB_APS_KEY_PAIR_COUNT` entries, default 1, since an end device shares a link key only with the trust center. Incoming frame counters are kept in RAM, as in NWK. The outgoing APS frame counter is global to the node (section 13).
- `zzb_apsme_set_tc_addr(node, addr)` sets apsTrustCenterAddress and, when the table has no entry for the address, adds one with the preconfigured key, global and provisional. `zzb_apsme_set_link_key(node, addr, key, attributes, type)` adds or replaces the entry of addr with incoming frame counter 0. Both save their records and return `-ENOMEM` when the table is full.
- BDB 10.2.5 step 9 replaces key A by key B in the trust center entry, unique and unverified. Key A is not kept, although the note after step 13 keeps it valid until B is verified: when the verification fails BDB leaves the network, and the next join uses the preconfigured key.
- `zzb_aps_reset(node)` clears the AIB, the key-pair table and the duplicate rejection table and deletes the APS records. The transmission table, the APS counter and the frame counter stay; transmissions in progress end on their own, since NLDE-DATA fails after a leave. BDB resets call it (milestone 11).

### Binding table

- An entry holds the source endpoint, the cluster, the destination address mode (group or IEEE address), the group address, the IEEE address and the destination endpoint. The source is always the node, so its address is not stored. The table has `CONFIG_ZZB_APS_BINDING_COUNT` entries, default 8.
- `zzb_apsme_bind_req(node, binding)` returns 0, also when the same entry exists, `-EINVAL` for another destination address mode and `-ENOMEM` when the table is full. `zzb_apsme_unbind_req(node, binding)` returns 0 or `-ENOENT`. Both save or delete the record of the entry. `zzb_aps_reset()` clears the table and deletes the records.
- APSDE-DATA with destination address mode 0x00 (R22 2.2.4.1.1) uses the entries with the source endpoint and the cluster of the request. A group entry gives a group-addressed frame to 0xfffd; an IEEE address is resolved as in mode 0x03. Entries whose address does not resolve or is the node itself are skipped; the local indication of R22 2.2.4.1.1 is not implemented. Without a destination left the request returns `-ENOENT` and produces no confirm, by the rule of Data service, where R22 issues a confirm with NO_BOUND_DEVICE or NO_SHORT_ADDRESS.
- TxOptions security returns `-ENOTSUP` in mode 0x00: an end device shares a link key only with the trust center, and group frames do not use one. Acknowledged transmission applies to the IEEE destinations only.
- Every destination needs a free entry of the transmission table and a buffer for its frame, which has its own header; both are taken before the first frame is sent, and the request returns `-ENOBUFS` when one is missing. `CONFIG_ZZB_APS_TX_COUNT` limits the destinations of one request.
- Every destination is a transmission of its own with its own APS counter, since frames with the same counter to a device bound twice would meet its duplicate rejection table (R22 2.2.8.4.2). Acknowledgements and retries follow Data service; an unacknowledged destination makes one attempt. A synchronous error of the first destination is returned and nothing is sent; a later destination whose frame cannot be sent follows the rules of a failed attempt.
- The transmission entries of one request carry the index of its first entry. An entry that ends stays in use until the last one ends; then all are freed and one `zzb_apsde_data_conf()` follows, with SUCCESS when every destination succeeded, otherwise the status of the first failed destination in the order of the binding table. R22 does not define the status for several destinations.

### Frame security

R22 4.4.1:

- `zzb_aps_secure(node, buf, hdr_len, key, key_id, ext_nonce)` secures a buffer whose data starts at the APS header. Key identifier 0 uses the key as data key, 2 the key-transport key HMAC(key, 0x00), 3 the key-load key HMAC(key, 0x02). The function takes the next APS frame counter, inserts the auxiliary header after the APS header through the headroom and appends the MIC. Commands have the extended nonce; data frames have it when TxOptions requests it.
- Frames are encrypted only with provisional or verified entries (R22 4.4.1.1 step 1); acknowledgements are the exception (Reception).
- The sender of an incoming frame is the source address in the auxiliary header. Without the extended nonce it is apsTrustCenterAddress for network address 0x0000, otherwise the neighbor with the network source address, since nwkAddressMap does not exist yet; other frames without the extended nonce are dropped. Z-Stack Home 1.2 sends Transport Key without the extended nonce from the parent, although R22 4.4.1.1 requires it for commands.
- The frame is decrypted with the entry of the sender, whatever its key attributes, since Confirm Key arrives under the unverified key B. Without an entry it is decrypted with the preconfigured key while the network key is pending (R22 4.4.1.2 step 2). A failed decryption does not fall back to the preconfigured key.
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

`CONFIG_ZZB_ZDO` builds `zdo/`. Milestone 10 implements the ZDO server of an end device, the ZDO client for Node_Desc_req and Device_annce, and the processing of the APS key indications. BDB (milestone 11), part of the ZDO application object (section 5), defines the NLME callbacks, `zzb_apsme_confirm_key_ind()` and the confirms of the ZDO client.

### Key indications

- A Transport Key with a network key: `zzb_apsme_set_tc_addr(src)`, then on success `zzb_nlme_set_nwk_key(key, seq)`. When the first call fails (`-ENOMEM`), the key stays unset and BDB ends the join by its timeout.
- A Transport Key with a trust center link key is dropped until BDB, which decides whether to install it (BDB 10.2.5 step 9, 10.2.6).
- Switch Key: `zzb_nlme_set_active_key(seq)`.

### Frames

- ZDO frames use endpoint 0 and profile 0x0000, and their ASDU starts with the transaction sequence number (TSN). A response copies the TSN of the request, has the request cluster with bit 15 set and goes to the network source of the request (destination address mode 0x02).
- The TSN is one counter per node with a random initial value; it advances with every frame ZDO originates (requests, Device_annce).
- Every unicast ZDO frame requests an APS acknowledgement (R23 2.4.2.8.4). R22 requires one only on the replies to broadcast discovery requests (R22 2.4.2.1); the R23 rule includes that case and treats all frames alike. Broadcast frames go without. ZDO frames have NWK security only; requests secured with a link key are accepted too.
- ASDU handles: 0xfe for Mgmt_Leave_rsp, 0xff for the other ZDO frames; values below 0xfe belong to the application (ZCL). APSDE-DATA.confirm with handle 0xff is ignored.
- A frame that cannot be sent (no buffer, an APSDE-DATA error) is dropped; the requester repeats its request.
- `ZZB_APS_MAX_ASDU` (82) is the longest ASDU of a unicast frame with NWK security only (section 7). The node descriptor and the record count of the management responses use it.

### Reception

- `zzb_apsde_data_ind()` passes frames to endpoint 0 with profile 0x0000 and a non-empty ASDU to ZDO. Frames to the application endpoints and to the broadcast endpoint 0xff are dropped until ZCL (milestone 12).
- A request is a broadcast when its network destination is a broadcast address. No error status answers a broadcast request (R22 2.4.4.1).
- Broadcasts of the requests R22 allows only as unicast are dropped: Node_Desc, Power_Desc, Simple_Desc, Active_EP (R22 2.4.2.1), Bind, Unbind, Mgmt_Lqi, Mgmt_Bind and Mgmt_Leave.
- A unicast request without a handler gets a response of the TSN and NOT_SUPPORTED (0x84); such a broadcast is dropped (R22 2.4.4.1). A supported request with a short payload is dropped.
- Responses (cluster bit 15 set) go to the transaction table of the client; responses that match no transaction are dropped.
- A received Device_annce is dropped. R22 2.4.4.2 updates nwkAddressMap from it, which does not exist; address conflict detection from Device_annce is optional for an end device (PICS NLF31, NLF32). A binding to an IEEE address other than the trust center or a neighbor is therefore accepted but skipped when sending (section 11). This affects device-to-device bindings and finding & binding as initiator (milestone 13); nwkAddressMap is added when such a case needs it.
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

- The power descriptor reports current power mode 0 (receiver synchronized with RxOnWhenIdle), the power source of the device definition as available and current source, and level 100 % (0xc). The battery level comes with the Power Configuration cluster (milestone 14).
- Node_Desc_req, Power_Desc_req and Active_EP_req for another network address get INV_REQUESTTYPE without a descriptor; Active_EP_rsp then has count 0. Active_EP_rsp lists the endpoints in the order of the device definition.
- Simple_Desc_req: endpoint 0 or 0xff gives INVALID_EP, an endpoint not in the device definition NOT_ACTIVE, another network address INV_REQUESTTYPE, all with length 0.
- Match_Desc_req (R22 2.4.4.2.7) for the network address of the node or a broadcast address matches an endpoint whose profile equals the requested one, or any profile when the request has 0xffff, and which has an input cluster of the request input list or an output cluster of the request output list. A request for another address gets INV_REQUESTTYPE as unicast, with the network address and match length 0 as in Figure 2-70 (the text of 2.4.4.2.7 omits the address), and is dropped as broadcast. Without a match a broadcast gets no response and a unicast gets SUCCESS with match length 0.

Bindings (R22 2.4.3.2.2, 2.4.3.2.3):

- A source address other than the EUI-64 of the node gives NOT_SUPPORTED, a source endpoint outside 1 to 254 INVALID_EP, a destination address mode other than 0x01 and 0x03 NOT_SUPPORTED (R22 does not define this case). Then APSME-BIND or APSME-UNBIND runs (section 11): a full table gives TABLE_FULL, a missing entry NO_ENTRY.
- No authorization rule applies: R22 has none, and R23 restricted mode is excluded (section 1).

Management (R22 2.4.3.3, 2.4.4.4):

- Mgmt_Lqi_rsp lists the neighbor entries with a relationship. A record holds nwkExtendedPANID, the extended address (0xffffffffffffffff when unknown), the network address, the device type, RxOnWhenIdle (1 for a coordinator or router, 2 unknown for an end device), the relationship, permit joining 2 (unknown), the depth (0 for the coordinator, from the beacon for a router) and the LQI. The NWK record does not store the depth, so after a reboot a router parent is reported with depth 0; the router (milestone 15) needs the depth of its parent for its own beacon and stores it.
- Mgmt_Bind_rsp lists the binding entries with the EUI-64 of the node as source address; an empty table gives SUCCESS with zero counts and no list.
- Both responses hold as many whole records from the start index as fit in `ZZB_APS_MAX_ASDU`: 3 neighbor records, or 3 bindings with an IEEE destination. A start index past the end gives SUCCESS with list count 0. R22 Table 2-129 limits the neighbor records to two, a value derived from the 802.15.4-2003 frame size.
- Mgmt_Permit_Joining_req, mandatory (R22 Table 2-126), gets INVALID_REQUEST (0xc2) as unicast, the NLME-PERMIT-JOINING.confirm status of an end device (R22 3.2.2.7.3).
- Mgmt_Leave_req (R22 2.4.3.3.5, 3.6.1.10.3.1) for the EUI-64 of the node or 0 gets Mgmt_Leave_rsp SUCCESS with handle 0xfe. Its APSDE-DATA.confirm with any status, or the failure to send it, issues `zzb_nlme_leave_req(node, rejoin)`; an NLME error is ignored. Remove children is ignored. The rejoin flag comes from the request, as in R23; R22 sets it to FALSE. A coordinator that sets rejoin expects the device back, and removal from ZHA sends rejoin 0, so both readings behave alike there. ZDO keeps the flag, and BDB reads it at `zzb_nlme_leave_conf()`: without rejoin it performs the reset of BDB 9.4, with rejoin it rejoins. Another device address gets UNKNOWN_DEVICE (0xc8), the NLME-LEAVE.confirm status for a device that is not a child (R22 3.2.2.18.3).
- Mgmt_NWK_Update_req (R22 2.4.3.3.9, Annex E) with scan duration 0xfe changes the channel. The channel mask must have exactly one bit of channels 11 to 26 on page 0; otherwise a unicast gets Mgmt_NWK_Update_notify INVALID_REQUEST (0xc2). A valid request gets no response. After nwkNetworkBroadcastDeliveryTime (`ZZB_NWK_BROADCAST_DELIVERY_MS`, 9 s at 2.4 GHz) ZDO calls `zzb_nlme_set_channel()` with the nwkUpdateId of the request; an error is ignored, since BDB finds the network by a rejoin on all channels (section 16). A new request during the wait replaces the pending one. R22 Annex E increments nwkUpdateId; ZDO copies it from the request, as R23 Annex E does, because the beacons of the network carry the value of the network manager and parent selection for a rejoin compares it with nwkUpdateId (section 10). The copy equals that value also after a missed update.
- Scan duration 0xff is dropped: the stack keeps neither apsChannelMaskList nor nwkManagerAddr, and an end device that missed a channel change finds the network by a rejoin on all channels (section 16).
- Scan durations 0x00 to 0x05 request an energy scan, which the MAC does not have: a unicast gets Mgmt_NWK_Update_notify NOT_SUPPORTED with the status only, a broadcast is dropped (PICS AZD801: optional for end devices). Other scan durations get INVALID_REQUEST as unicast. The transmission and failure counters of Annex E are not kept.

### Client

- The transaction table has `CONFIG_ZZB_ZDO_TRANSACTION_COUNT` entries (default 4) with the TSN, the request cluster, the destination and the deadline. One `k_work_delayable` serves the table, rescheduled for the earliest deadline with `k_work_reschedule_for_queue()`.
- `zzb_zdo_node_desc_req(node, addr)` sends Node_Desc_req for addr to addr with an APS acknowledgement request. It returns `-ENOBUFS` when the table is full or no buffer is free, and the APSDE-DATA errors; otherwise exactly one `zzb_zdo_node_desc_conf(node, status, addr, server_mask)` follows. BDB reads the stack compliance revision from the server mask (BDB 10.2.5 step 5); other fields are added when a user needs them.
- A response matches by TSN, by the request cluster with bit 15 set and by a network source equal to the destination of the request. The confirm carries the status of the response, with the server mask valid on SUCCESS, or TIMEOUT (0x85) `ZZB_ZDO_RSP_TIMEOUT_MS` (5000 ms) after the request was sent. 5 s is bdbcTCLinkKeyExchangeTimeout, so BDB 10.2.5 steps 3 and 4 use the confirm as their timer. A SUCCESS response with a short descriptor is dropped, and the transaction waits for the timeout.
- The entry is freed before the confirm. APSDE-DATA.confirm of a client request is not used: the response or the timeout ends the transaction. Three APS attempts fit into 5 s; the response to the fourth usually comes too late, and the caller repeats the request (BDB 10.2.5 step 4).
- `zzb_zdo_device_annce_req(node)` broadcasts Device_annce to 0xfffd without acknowledgement, with nwkNetworkAddress, the EUI-64 and nwkCapabilityInformation (R22 2.4.3.1.11), and returns an error code. BDB calls it after a join or rejoin (BDB 7.1 step 5, 8.2 step 11) and after an address change (R22 3.6.1.9.2).

## 13. Security and frame counters

`sec/` contains pure functions on top of PSA Crypto:

- AES-128-ECB (`PSA_WANT_ALG_ECB_NO_PADDING`) for the Matyas-Meyer-Oseas hash (R22 Annex B.6). Only the padding for messages shorter than 2^16 bits is implemented; Zigbee hashes at most 32 bytes;
- AES-CCM with a 4-byte tag, `PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4)`, for security level 5 (ENC-MIC-32), the only level used on air. The CCM primitive takes the MIC length, so the R22 Annex C vectors (M=8) apply to it. The frame functions on top of it parse the auxiliary header, build the nonce and apply the level-field rule (section 7);
- HMAC-MMO with a 16-byte key and a 1-octet input, the only form R22 uses: key-transport key (0x00), key-load key (0x02), verify-key hash (0x03) (R22 4.5.3, 4.4.10.7.4, Annex B.1.4);
- install code CRC (CRC-16/X-25 with Zephyr `crc16_ccitt()`, BDB 10.1.1) and install code key derivation (BDB 10.1.2).

CCM functions take a `psa_key_id_t`. The PSA key cache in `sec/key_cache.c` serves all nodes: `zzb_sec_key_get(key, &id)` returns the ID of an imported key with the same 16 bytes, or imports the key with the policy `PSA_ALG_AEAD_WITH_AT_LEAST_THIS_LENGTH_TAG(PSA_ALG_CCM, 4)` into the least recently used of `CONFIG_ZZB_KEY_CACHE_SIZE` entries (default 4). The ID is valid until the next call on the stack thread. Tables store the 16 key bytes. Zephyr builds PSA with `MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS`, under which input and output buffers must not overlap, so in-place CCM copies its input to a local buffer first. The auxiliary header codec writes the security level sub-field as 0; the frame functions set it to 5 for the CCM operation.

Keys are stored as arrays: two network keys and N link-key pairs. The trust center is identified by `apsTrustCenterAddress` (IEEE address). Code must not assume that the trust center is the parent. Frames to the trust center go to network address 0x0000, since R22 4.6.2.2 places the trust center on the coordinator in a centralized network.

Outgoing frame counters: one for NWK and one for APS, global to the node, both implemented by `common/fc.c`. The NWK counter must survive factory reset, leave and joining another network (R22 4.3.4). Both are persisted as a watermark under `zzb/<n>/fc/nwk` and `zzb/<n>/fc/aps`: a version byte (1) followed by the watermark, little-endian, 4 bytes. Factory reset (milestone 11) erases `zzb/<n>/nwk`, `zzb/<n>/aps`, `zzb/<n>/bdb` and `zzb/<n>/zcl` and keeps `zzb/<n>/fc`.

- On boot: counter = watermark, then `zzb_node_init()` saves watermark + `CONFIG_ZZB_FRAME_COUNTER_JUMP` (default 1024). It returns the error if the save fails or the record has an unknown version or length.
- At runtime: when the counter reaches watermark - JUMP/2, a work item on the stack queue saves counter + JUMP and moves the watermark when the save succeeds. After a failed save, the next secured frame schedules the save again. While the counter equals the watermark, securing a frame fails with `-EBUSY`; a counter of 2^32-1 fails with `-EOVERFLOW` (R22 4.3.1.1 step 1).

Incoming frame counters are kept in RAM, in the neighbor table (section 10) and the key-pair table (section 11).

## 14. Persistence

Zephyr settings: NVS on flash parts (nRF52840, ESP32-C6/H2), ZMS on RRAM parts. Each record has its own key and a version field, since writes are atomic per key. Tables that grow on a trust center store one key per entry.

Persisted records:

- NWK: PAN ID, extended PAN ID, short address, channel, nwkUpdateId, parent, network keys and key sequence number;
- APS: link keys, trust center address, binding table, group table;
- BDB: `bdbNodeIsOnANetwork`;
- ZCL: non-volatile attributes and reporting configuration;
- frame counter watermarks.

ZHA writes bindings and reporting configuration once, during pairing. Losing them on reboot stops attribute reports without an error on either side.

NWK records, little-endian:

| Key | Contents | Length |
|---|---|---|
| `zzb/<n>/nwk/net` | version (1), PAN ID, extended PAN ID, network address, channel, nwkUpdateId, nwkCapabilityInformation, nwkParentInformation, network address, IEEE address and device type of the parent | 28 |
| `zzb/<n>/nwk/keys` | version (1), active slot, slot written last, two slots of valid flag, key sequence number and key | 39 |

- NWK writes its records only while the node is on a network and the key is not pending: when a key ends the pending state, after a secure rejoin, when nwkParentInformation or the address changes, when a key is set on the network and when a received frame switches the active key. Save errors are ignored; the next save writes the whole record again.
- The loader writes the NIB and the key slots directly, adds the parent entry with the extended PAN ID, PAN ID, channel and device type, calls MLME-SET for the PAN ID, the address and the channel, and starts the keepalive. A record with an unknown version or length makes `zzb_node_init()` return `-EINVAL`.
- A leave without rejoin deletes both records.

APS records, little-endian:

| Key | Contents | Length |
|---|---|---|
| `zzb/<n>/aps/tc` | version (1), apsTrustCenterAddress | 9 |
| `zzb/<n>/aps/key/<i>` | version (1), device address, key attributes, link key type and link key of key-pair entry i | 27 |
| `zzb/<n>/aps/bind/<i>` | version (1), source endpoint, cluster, destination address mode, destination address (8 bytes, a group address in the first two) and destination endpoint of binding entry i | 14 |

- APS saves the trust center address in `zzb_apsme_set_tc_addr()`, and an entry when `zzb_apsme_set_tc_addr()` or `zzb_apsme_set_link_key()` adds or changes it or a Confirm Key verifies it. `zzb_apsme_bind_req()` saves a binding entry, `zzb_apsme_unbind_req()` deletes it. Save errors are ignored. `zzb_aps_reset()` deletes the records.
- A record with an unknown version or length makes `zzb_node_init()` return `-EINVAL`.

## 15. ZCL data model

- Attribute metadata is constant and declared with macros: cluster ID, attribute ID, type, access, flags, offset into the storage struct.
- Attribute values live in a struct per cluster instance, declared by the application. The endpoint list is part of the device definition passed to `zzb_node_init()`.
- `BUILD_ASSERT` checks attribute types against storage field sizes.
- Simple_Desc_rsp and Active_EP_rsp are generated from the same tables. Cluster macros add the mandatory attributes, including ClusterRevision (0xFFFD).
- The application changes attributes with `zzb_zcl_attr_set()`. The call is marshaled to the stack thread and runs the reportable-change check and the settings save. Other threads must not write the storage structs.
- An optional per-cluster callback validates remote writes.
- ZCL is a Kconfig option. An NCP build leaves ZCL to the host.
- Cluster tables may be generated from the project-chip/zap ZCL XML (`zcl-builtin/silabs`, Apache-2.0, ZCL rev 8) and checked against zigbee-herdsman `src/zspec/zcl` (MIT).

## 16. Sleepy end device and power management

- The stack does not call PM APIs. It stops the radio between polls and arms only deadline timers; Zephyr tickless idle does the rest.
- nRF52840: System ON idle with the radio stopped. The MAC uses `IEEE802154_CONFIG_RX_ON_WHEN_IDLE`.
- ESP32-C6/H2: `CONFIG_PM` and `CONFIG_IEEE802154_ESP32_SLEEP_ENABLE`. With sleep enabled the driver does not report `IEEE802154_RX_ON_WHEN_IDLE`, so the MAC opens the post-poll receive window itself: `start()`, wait up to macMaxFrameTotalWaitTime when the ACK had frame pending set, `stop()`.
- A zero-length data frame from the parent means no pending data; the device returns to sleep.
- Poll rate (BDB 6.8): at most 3 s between polls while a response is expected, during joining and during finding & binding. A parent holds a buffered frame for 7.5 s.
- An End Device Timeout Request follows every join and rejoin, and the keepalive method comes from the parent information field of the response (section 10). Milestone 14 chooses a timeout longer than the slow poll interval.
- Parent loss, reported by NWK after a keepalive failure or `CONFIG_ZZB_NWK_PARENT_FAILURES` failed transmissions (section 10): BDB rejoins securely on the current channel, then securely on all channels, then with a trust center rejoin. This also recovers from a channel change missed while asleep.

## 17. Extension points for coordinator and trust center

- Join, rejoin and leave are split into joiner-side and parent-side files sharing the frame codecs. Parent-side files are compiled with `zephyr_library_sources_ifdef(CONFIG_ZZB_ROUTER_CAPABLE ...)`. Small branches use `IS_ENABLED()`; functions contain no `#ifdef`.
- The parent has one "device joined" path. It sends APS Update Device to a remote trust center, or calls the local trust center through `node->tc` ops, which are NULL when `CONFIG_ZZB_TRUST_CENTER` is off. Children of the trust center itself never produce an Update Device frame on air. Received Request Key and Verify Key commands use the same dispatch.
- APS Tunnel is part of the router (milestone 16): a remote trust center reaches a joining grandchild through it.
- Public API tiers: (1) raw APSDE and endpoint registration with a raw handler; (2) ZDO client; (3) ZCL and BDB. An NCP build uses tiers 1 and 2 plus configuration get/set.
- Export and import of keys, frame counters and the device table are defined together with persistence, because zigpy relies on network backup and restore.
- NCP framing is independent of the transport (UART, USB CDC, TCP). Protocol candidates: ZBOSS NCP (zigpy-zboss exists), Z-Stack MT/ZNP (zigpy-znp exists), or a new protocol with a new zigpy radio library. The choice is made at milestone 20.

## 18. Reference code

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

## 19. Testing

- Unit tests: ztest on `native_sim/native/64`; CI also runs `native_sim` (32-bit) to catch pointer-size errors.
- Crypto and codec tests use R22 Annex C vectors, BDB 10.1 install code examples and frames captured from real networks. The first frames come from the Ziggurat and zigbee-rs test suites (listed in `NOTICE`) and from a test network whose network key may be published. MAC frames missing from these sources are built from the IEEE 802.15.4-2011 figures and R22 Tables D-1 to D-3.
- Multi-node integration tests: fake radio devices implementing `ieee802154_radio_api` (`tests/common/fake_radio.c`), connected by an in-process medium. Each fake radio is a net device for the custom-L2 backend with its own `net_if`, since the raw backend serves one port. The medium has a link matrix, delivers a frame only to radios on the same channel and filters by PAN ID and destination address; frame loss is added with the router MAC (milestone 15). It logs every frame with `LOG_HEXDUMP_DBG`. The MAC calls the radio API in the same way as on hardware. native_sim time is simulated, so timer behavior is deterministic.
- A test can replace the interface of a fake radio by a callback, which receives the frames the radio accepts and returns the frame pending bit of the ACK, and can send frames from that radio with `fake_radio_send()`.
- `tests/glue/raw` tests the radio port in raw mode against FFF fakes of the radio API: TX modes, software CSMA-CA, status mapping, frame pending from the ACK and the RX copy. `tests/glue/custom_l2` tests the custom-L2 backend with two fake radios.
- `tests/unit/mac` tests the device MAC in raw mode against FFF fakes of the radio API. The `tx()` fake plays the parent: it returns the ACK through `ieee802154_handle_ack()`, with frame pending when the test sets it, and injects replies with `net_recv_data()`. It simulates frame loss by withholding the ACK.
- `tests/unit/nwk` tests NWK with the MAC and APS turned off, against FFF fakes of MCPS, MLME and the NLDE and NLME callbacks. Node 1 plays the parent: the test builds its frames, secured commands included, with the NWK send functions and passes the frames caught by the MCPS fake to node 0.
- `tests/unit/aps` tests APS with NWK and ZDO turned off, against FFF fakes of NLDE-DATA.request, the NWK functions APS reads and the APSDE and APSME callbacks. Node 0 has the destination address of the captured Transport Key and node 1 its source address. Node 1 plays the trust center and secures its frames with `zzb_aps_secure()` and explicit keys.
- `tests/unit/zdo` tests ZDO with APS and NWK turned off, against FFF fakes of APSDE-DATA.request, the APSME and NLME functions ZDO calls and the confirm of the ZDO client. The test fills the neighbor and binding tables of the node directly, passes requests to `zzb_apsde_data_ind()` and confirms to `zzb_apsde_data_conf()`, and checks the frames caught by the APSDE fake.
- `tests/build/radio` is a build-only consumer application. It enables the module with NVS as the settings backend and, on nrf52840dk and ESP32-C6/H2, also the 802.15.4 driver in raw mode. A second scenario builds nrf52840dk with the custom-L2 backend. The application passes the `zephyr,ieee802154` chosen device and a device definition with one endpoint to `zzb_node_init()`, and defines the callbacks of BDB until BDB defines them.
- CI runs on Woodpecker (`.woodpecker/build.yaml`): twister runs the tests on `native_sim` and `native_sim/native/64` and only builds the hardware targets.
- `tests/common/scripted_tc.c` plays the trust center side of join sequences captured with a sniffer from EZSP and Z-Stack coordinators on a test network with a known network key.
- Reboot and power-loss tests run on NVS over the flash simulator, kept in RAM with `--flash_in_ram` so that every run starts with erased flash; the node is destroyed and re-initialized. In the NWK tests a reboot is a second `zzb_nwk_init()` followed by loading the settings.
- A test asserts that no timer is armed while a sleepy node is idle.
- `tests/integration/nwk_data` runs two nodes on fake radios with the custom-L2 backend. Node 1 is a child of node 0; the test sets up both with NLME-SET and the neighbor table functions and, with APS turned off, sends APS frames built by hand.
- `tests/integration/nwk_join` runs one node on fake radio 1 against a parent scripted in the test on fake radio 0. The script answers inside the `tx()` of the child: a beacon to a beacon request, an association response to the data request, the End Device Timeout Response and rejoin responses, and it secures and checks frames with the `sec/` functions. Frames it sends on its own, such as a leave request, are built on the stack thread with `zzb_call_sync()`, since a PSA key ID is valid only there. The parent also plays the trust center: after the association it sends the network key in a Transport Key secured with the key-transport key of the preconfigured key, and the APS suite runs the trust center link key exchange, acknowledgements in both directions, a retransmission, a duplicate and a network key switch. ZDO is turned off, and the APS callbacks of the test set apsTrustCenterAddress, the network key, the active key and the link key, as ZDO and BDB do. ZDO frames on air are tested with BDB (milestone 11). Every test starts with NLME-RESET, `zzb_aps_reset()` and a join, because ztest orders tests by name. All secured frames use the keys of `ci/check-frame-log.sh`; frames with a wrong MIC stay in the unit tests.
- text2pcap and tshark decode the frame log of the medium and check MICs, as a decoder independent of the stack. The CI step `frame-log` runs after `native_sim` in the `antrea/tshark` image (Ubuntu 24.04, tshark 4.2.2), pinned by digest. `ci/check-frame-log.sh` takes the hexdumps of the medium from `handler.log`, builds a pcap with `text2pcap -l 230` (IEEE 802.15.4 without FCS) and decodes it with the test network keys and link keys; tshark derives the key-transport and key-load keys itself. The step fails if the log holds no frame with security, or a frame with NWK or APS security has fewer decrypted keys (`zbee.sec.key`) than auxiliary headers (`zbee.sec.key_id`). An APS ACK with APS security may have one key fewer, since tshark does not decrypt its empty payload.
- Hardware interoperability: joining ZHA with EZSP and Z-Stack coordinators from milestone 11; current measurement from milestone 14.

## 20. Milestones

Each milestone is one feature branch with its tests.

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
11. BDB end device: initialization and boot rejoin (7.1), steering of a node not on a network (8.2), TCLK exchange (10.2.5), resets (chapter 9) including the reset after Mgmt_Leave_req (9.4), parent-loss rejoin; the NLME and Confirm Key callbacks, Device_annce after a join, a rejoin and an address change; ZDO frames on air in an integration test. Interop: join ZHA on EZSP and Z-Stack and stay joined across reboot.
12. ZCL foundation (read, write, discover attributes, default response), attribute macros, Basic, Identify, On/Off servers. Interop: ZHA creates the entity.
13. Attribute reporting with persistence; finding & binding (BDB 8.4, 8.5).
14. Sleepy end device: poll scheduler with holds, radio off between polls, timeout negotiation, Power Configuration and Poll Control clusters.
15. Router, part 1: MAC coordinator functions (beacons, association response, indirect queue, frame-pending table), stochastic address allocation, child aging and End Device Timeout server, permit join, Parent_annce; esp32 driver patch; frame loss in the fake medium.
16. Router, part 2: route discovery, link status, broadcast transaction table with passive ACK, many-to-one routes, route record, source-route relay, APS Update Device and Tunnel.
17. OTA Upgrade client with MCUboot (`flash_img`, `boot_request_upgrade()`).
18. Coordinator: network formation (energy scan, PAN ID selection), concentrator and source route table, network manager.
19. Trust center (BDB 10.3): key generation with `psa_generate_random()`, join and rejoin policy, network key update and switch, per-entry storage.
20. NCP; Green Power proxy basic.

## 21. Risks

| Risk | Mitigation |
|---|---|
| The stack passes its own tests but fails with real coordinators | ZHA interop from milestone 11; fixtures from sniffer captures; independent decoder in CI |
| esp32 driver: no frame-pending control, no custom-L2 branch, no CCA-only TX mode, `tx()` returns 0 after CCA failure and missing ACK, RX_ON_WHEN_IDLE withheld with sleep enabled, binary blobs | raw mode, software retries, zero-length frame fallback, ACK detected through `ieee802154_handle_ack()`, upstream patch, blob fetch in CI |
| Frame counter or persistence loss: the device cannot rejoin, or reports stop | watermark key outside the factory-reset subtree; reboot and power-loss tests; one settings key per record |
| Router and coordinator scope | ZigBee PRO/2007 layer PICS (08-0006-05) as a per-milestone checklist; features outside the ZHA path deferred |
| Sleepy timing and current are not observable on native_sim | driver RX_ON_WHEN_IDLE where available; deadline-only timers with an idle test; current measurement on hardware |

New dependencies accepted with this document: PSA Crypto (Zephyr mbedTLS / TF-PSA-Crypto), settings with NVS or ZMS, FFF, `net_buf`. The `antrea/tshark` container image is accepted for the frame log check (milestone 7). Zephyr shell and MCUboot are confirmed at the milestones that introduce them.
