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
  common/   buf.c queue.c timer.c eui64.c eui64_esp32.c node.c
  radio/    port.c raw.c custom_l2.c
  mac/      frame.c mac.c scan.c assoc.c poll.c tx.c
            coord/   beacon.c assoc_resp.c indirect.c fpb.c
  nwk/      nwk.c frame.c nib.c neighbor.c addr_map.c security.c join.c rejoin.c leave.c
            ed_timeout.c fc.c
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

The device definition (endpoints, clusters, attribute storage) is passed to `zzb_node_init()`. The stack does not use global iterable sections. `zzb_node_init()` loads the settings records of the node with `settings_load_subtree_direct("zzb/<n>")`, where n is the index of the node in the pool, and passes each key to its layer. The stack registers no settings handler, so a `settings_load()` call of the application does not reach a node before it exists.

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
- Unit tests replace adjacent layers by link-time substitution with the FFF bundled with Zephyr (`zephyr/fff.h`): a test links the sources of the layer under test and defines fakes with the names of the neighbor layer's functions. Zephyr links libzzb with `--whole-archive`, so each layer has a Kconfig option that removes it from the build. `CONFIG_ZZB_MAC` defaults to y when a radio backend is enabled; the glue tests turn it off because they fake `zzb_radio_rx()` and `zzb_radio_tx_done()`. `CONFIG_ZZB_NWK` defaults to the value of `CONFIG_ZZB_MAC`; the MAC tests turn it off, and the NWK tests turn it on with the MAC off. APS and the layers above get their options when they are added.

## 6. Tables

| Layer | Tables |
|---|---|
| MAC | PIB, TX queue, indirect queue, mirror of the driver frame-pending table |
| NWK | neighbor table (parent and children as entries with relationship, timeout, keepalive, incoming frame counter), `nwkAddressMap`, route table, route discovery table, broadcast transaction table, route record table, network keys (two), group ID table |
| APS | binding table, group table, device key-pair table (incoming frame counter, verified state), trust center IEEE address, duplicate rejection table |
| ZDO | descriptors generated from the device definition, transaction table |
| ZCL | reporting slots |

Every table is accessed through find/add/remove functions. The coordinator can replace the linear arrays with indexed storage behind the same functions.

## 7. Buffers

- One frame is one linear `net_buf` from a stack pool; the PSDU is at most 127 bytes. The buffer user data is `struct zzb_frame_meta`: source and destination MAC addresses (addressing mode, PAN ID, short and extended address), MSDU handle, LQI, RSSI and flags (ACK request, indirect transmission, frame pending). Headroom covers the MAC, NWK, auxiliary security and APS headers; the MIC is appended at the tail.
- RX: the radio port copies the PSDU out of the driver `net_pkt` and releases the `net_pkt` immediately, returning it to the driver RX pool.
- TX: each radio port embeds a zeroed `struct net_pkt` that is not taken from the `net_pkt` slab, and passes it to every `tx()` with the stack buffer as the fragment. The drivers read the frame from the fragment and only control block flags from the packet.
- Ownership: a request returning a negative error leaves the buffer with the caller. A request returning 0 takes the buffer and produces exactly one confirm. An indication passes the buffer to the upper layer.
- MCPS-DATA.request needs `ZZB_MAC_HDR_MAX_LEN` bytes of headroom. It returns `-EMSGSIZE` when the headroom is shorter than the MAC header or the frame is longer than 125 bytes (aMaxPHYPacketSize without the FCS). MCPS-DATA.confirm carries the MSDU handle, and the MAC releases the buffer.
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
- MLME-SCAN, MLME-ASSOCIATE and MLME-POLL exclude each other: a second request returns `-EBUSY`. Each procedure allocates one command buffer and reuses it for its frames. MCPS-DATA.request always enqueues. A data frame enqueued during a scan is sent on the channel being scanned, so NWK must not send data while it scans.
- The source address of a transmitted frame is the short address when macShortAddress is below 0xfffe, otherwise the extended address (802.15.4 5.1.6.1).
- Active scan (802.15.4 5.1.2.1.2): the PAN ID filter is 0xffff during the scan. On each channel the MAC sends a beacon request and listens for aBaseSuperframeDuration * (2^n + 1) symbols; after a failed beacon request it moves to the next channel. Frames other than beacons are dropped. Every beacon produces MLME-BEACON-NOTIFY.indication with the channel and the Superframe Specification field, without duplicate filtering. MLME-SCAN.confirm carries only the status: SUCCESS, or NO_BEACON when no beacon arrived. The PAN ID filter and the channel are restored after the scan.
- Association (802.15.4 5.1.3.1): the association request and its data request use the first header form of R22 Tables D-1 and D-2, with source PAN ID 0xffff and the extended source address. The data request follows macResponseWaitTime after the ACK of the request. After any failure macPANId is 0xffff again.
- Data poll (802.15.4 5.1.6.3): after an ACK with frame pending, the MAC waits for a data frame from the polled coordinator. A zero-length frame or the end of the wait gives NO_DATA. MLME-POLL.confirm precedes MCPS-DATA.indication. The frame pending bit of the received frame reaches NWK in the frame metadata, and NWK decides whether to poll again.
- Outside these procedures, data frames with a payload produce MCPS-DATA.indication. Zero-length data frames, beacons and commands are dropped.
- The receiver stays on between polls until milestone 14.
- MLME-SET of macPANId and macShortAddress (`zzb_mlme_set_pan_id()`, `zzb_mlme_set_short_addr()`) writes the PIB and the radio filter. The other PIB getters and setters and MLME-RESET are added with the NWK join (milestone 8).

| Time | Definition | Value at 2.4 GHz |
|---|---|---|
| Scan dwell | aBaseSuperframeDuration (960 symbols) * (2^n + 1) | 138.24 ms for n = 3, the value R22 D.7 recommends |
| macResponseWaitTime | 32 * aBaseSuperframeDuration | 491.52 ms |
| macMaxFrameTotalWaitTime | 802.15.4 Table 52 formula with macMinBE 5, macMaxBE 8 and macMaxCSMABackoffs 4 (R22 D.6), since the frame comes from the CSMA-CA of the parent | 9846 symbols, 157.54 ms |

The 2011 text of the macMaxFrameTotalWaitTime formula has 2^macMinBE in its second term. The value above uses 2^macMaxBE, because the backoffs after the first m use BE = macMaxBE.

## 10. NWK

`CONFIG_ZZB_NWK` builds `nwk/` and `storage/settings.c`. It selects `CONFIG_SETTINGS`; the application enables the settings backend (section 12).

### Frame codec

`nwk/frame.c` is a byte codec in the style of `mac/frame.c`.

- The header struct holds the frame type, the frame control flags (discover route, multicast, security, source route, destination IEEE address, source IEEE address, end device initiator), the destination and source addresses, radius, sequence number, the IEEE addresses, the multicast control field and the source route subframe. Transmitted frames have protocol version 2 (nwkcProtocolVersion). The parser rejects other protocol versions and inter-PAN frames with `-ENOTSUP` and the reserved frame type with `-EINVAL`. The relay list of the source route subframe is copied into an array of nwkMaxSourceRoute (12) entries; a longer list is rejected with `-ENOTSUP`.
- Commands: all 13 commands of R22 3.4, in a struct with a union like `struct zzb_mac_cmd`. Lists (link status entries, route record relays, network report PAN IDs, link power delta entries) are not decoded: the struct holds the entry count and a pointer to the first encoded entry, which points into the frame after parsing. The writer copies the encoded entries from that pointer.
- Beacon payload (R22 3.6.7): 15 octets. A Protocol ID other than 0 gives `-ENOTSUP`, a shorter payload `-EINVAL`.

### NIB and neighbor table

- The NIB holds the attributes that milestone 7 uses: nwkPANId, nwkNetworkAddress, nwkExtendedPANID, nwkSequenceNumber (random initial value, R22 3.6.2.1), nwkSecurityMaterialSet (two key slots with the key, its sequence number and a valid flag), nwkActiveKeySeqNumber and the slot written last. nwkIeeeAddress is the EUI-64 of the node, nwkSecurityLevel is the constant 5, and the default radius is 2 * nwkMaxDepth = 30. Other attributes are added with the milestones that use them.
- NLME-SET: `zzb_nlme_set_pan_id()` and `zzb_nlme_set_short_addr()` write the NIB and call MLME-SET; `zzb_nlme_set_ext_pan_id()`; `zzb_nlme_set_nwk_key(node, key, seq)` writes the key into the slot with the same sequence number, otherwise into the slot that is not active, and resets the incoming frame counters of that slot in every neighbor entry. The first key becomes the active key.
- A neighbor table entry holds the extended address, network address, relationship, LQI and two incoming frame counters, one per key slot (R22 4.3.1.2 bounds the counters by the number of keys times the number of neighbors). Device type, RxOnWhenIdle, timeout, keepalive and the network discovery fields come with milestone 8. The table has `CONFIG_ZZB_NEIGHBOR_COUNT` entries (default 16); add returns NULL when it is full.

### Frame security

Two functions secure and unsecure in place a buffer whose data starts at the NWK header (R22 4.3.1).

- Outgoing: the active key (none: `-ENOENT`), the next outgoing frame counter (section 11), an auxiliary header with key identifier 1, the extended nonce and the EUI-64 of the node, inserted after the NWK header by moving the header into the headroom, then CCM and the MIC at the tail.
- Incoming: the auxiliary header must have key identifier 1 and the extended nonce, otherwise `-EINVAL`. A frame counter of 2^32-1 or below the stored counter gives `-EALREADY`. The key slot is found by the key sequence number and the neighbor by the source address of the auxiliary header; a missing slot or neighbor gives `-ENOENT`. Frames from devices without a neighbor entry are therefore dropped; an end device receives a broadcast relayed by another router again from its parent. A MIC mismatch gives `-EBADMSG`. After verification the counter of the slot is set to the received counter + 1.
- A frame secured with the key of the slot written last, when that slot is not active, makes that key active (R22 4.3.1.2 step 7). A frame secured with the older key does not switch back.
- R22 4.3.1.2 step 8 (unauthenticated child) is part of the router (milestone 15).

### Data service

- `zzb_nlde_data_req(node, buf, dst_addr, radius, secure)` takes the NSDU after `ZZB_NLDE_HEADROOM` bytes of headroom and the NSDU handle in the frame metadata. It returns `-ENOTCONN` while the node has no network address or no parent. The header has frame type data, discover route 1 for unicast and 0 for broadcast, end device initiator 0 (nwkParentInformation arrives with milestone 8) and no IEEE address fields. Radius 0 selects the default radius.
- The MAC destination is the neighbor whose network address is the NWK destination, otherwise the parent. Unicast frames request a MAC ACK. A broadcast from an end device is a MAC unicast to the parent without ACK request (R22 3.6.5).
- Security and MCPS-DATA.request errors are returned by the request, which then leaves the buffer with the caller. MCPS-DATA.confirm produces `zzb_nlde_data_conf(node, handle, status)` with the MAC status.
- Received data frames addressed to the node, to 0xffff or to 0xfffd (the receiver stays on until milestone 14) produce `zzb_nlde_data_ind(node, buf, dst_addr, src_addr, security_use)` with the NSDU in the buffer and the LQI in the metadata. Frames with the network address of the node as source are dropped. Command frames are dropped until milestone 8. Every received frame updates the LQI of the neighbor whose network address matches the MAC source address.
- There is no broadcast transaction table until milestone 16. A repeated secured frame from the parent is rejected by the incoming frame counter check.
- `zzb_nlde_data_conf()` and `zzb_nlde_data_ind()` are defined by APS (milestone 9); until then the tests and `tests/build/radio` define them.

## 11. Security and frame counters

`sec/` contains pure functions on top of PSA Crypto:

- AES-128-ECB (`PSA_WANT_ALG_ECB_NO_PADDING`) for the Matyas-Meyer-Oseas hash (R22 Annex B.6). Only the padding for messages shorter than 2^16 bits is implemented; Zigbee hashes at most 32 bytes;
- AES-CCM with a 4-byte tag, `PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4)`, for security level 5 (ENC-MIC-32), the only level used on air. The CCM primitive takes the MIC length, so the R22 Annex C vectors (M=8) apply to it. The frame functions on top of it parse the auxiliary header, build the nonce and apply the level-field rule (section 7);
- HMAC-MMO with a 16-byte key and a 1-octet input, the only form R22 uses: key-transport key (0x00), key-load key (0x02), verify-key hash (0x03) (R22 4.5.3, 4.4.10.7.4, Annex B.1.4);
- install code CRC (CRC-16/X-25 with Zephyr `crc16_ccitt()`, BDB 10.1.1) and install code key derivation (BDB 10.1.2).

CCM functions take a `psa_key_id_t`. The PSA key cache in `sec/key_cache.c` serves all nodes: `zzb_sec_key_get(key, &id)` returns the ID of an imported key with the same 16 bytes, or imports the key with the policy `PSA_ALG_AEAD_WITH_AT_LEAST_THIS_LENGTH_TAG(PSA_ALG_CCM, 4)` into the least recently used of `CONFIG_ZZB_KEY_CACHE_SIZE` entries (default 4). The ID is valid until the next call on the stack thread. Tables store the 16 key bytes. Zephyr builds PSA with `MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS`, under which input and output buffers must not overlap, so in-place CCM copies its input to a local buffer first. The auxiliary header codec writes the security level sub-field as 0; the frame functions set it to 5 for the CCM operation.

Keys are stored as arrays: two network keys and N link-key pairs. The trust center is identified by `apsTrustCenterAddress` (IEEE address). Code must not assume that the trust center is the parent or has short address 0x0000.

Outgoing frame counters: one for NWK and one for APS, global to the node. The NWK counter must survive factory reset, leave and joining another network (R22 4.3.4). Both are persisted as a watermark under `zzb/<n>/fc/nwk` and `zzb/<n>/fc/aps`: a version byte (1) followed by the watermark, little-endian, 4 bytes. Factory reset (milestone 11) erases `zzb/<n>/nwk`, `zzb/<n>/aps`, `zzb/<n>/bdb` and `zzb/<n>/zcl` and keeps `zzb/<n>/fc`.

- On boot: counter = watermark, then `zzb_node_init()` saves watermark + `CONFIG_ZZB_FRAME_COUNTER_JUMP` (default 1024). It returns the error if the save fails or the record has an unknown version or length.
- At runtime: when the counter reaches watermark - JUMP/2, a work item on the stack queue saves counter + JUMP and moves the watermark when the save succeeds. After a failed save, the next secured frame schedules the save again. While the counter equals the watermark, securing a frame fails with `-EBUSY`; a counter of 2^32-1 fails with `-EOVERFLOW` (R22 4.3.1.1 step 1).

Incoming frame counters are kept in RAM, in the neighbor table (section 10).

## 12. Persistence

Zephyr settings: NVS on flash parts (nRF52840, ESP32-C6/H2), ZMS on RRAM parts. Each record has its own key and a version field, since writes are atomic per key. Tables that grow on a trust center store one key per entry.

Persisted records:

- NWK: PAN ID, extended PAN ID, short address, channel, nwkUpdateId, parent, network keys and key sequence number;
- APS: link keys, binding table, group table, trust center address;
- BDB: `bdbNodeIsOnANetwork`;
- ZCL: non-volatile attributes and reporting configuration;
- frame counter watermarks.

ZHA writes bindings and reporting configuration once, during pairing. Losing them on reboot stops attribute reports without an error on either side.

## 13. ZCL data model

- Attribute metadata is constant and declared with macros: cluster ID, attribute ID, type, access, flags, offset into the storage struct.
- Attribute values live in a struct per cluster instance, declared by the application. The endpoint list is part of the device definition passed to `zzb_node_init()`.
- `BUILD_ASSERT` checks attribute types against storage field sizes.
- Simple_Desc_rsp and Active_EP_rsp are generated from the same tables. Cluster macros add the mandatory attributes, including ClusterRevision (0xFFFD).
- The application changes attributes with `zzb_zcl_attr_set()`. The call is marshaled to the stack thread and runs the reportable-change check and the settings save. Other threads must not write the storage structs.
- An optional per-cluster callback validates remote writes.
- ZCL is a Kconfig option. An NCP build leaves ZCL to the host.
- Cluster tables may be generated from the project-chip/zap ZCL XML (`zcl-builtin/silabs`, Apache-2.0, ZCL rev 8) and checked against zigbee-herdsman `src/zspec/zcl` (MIT).

## 14. Sleepy end device and power management

- The stack does not call PM APIs. It stops the radio between polls and arms only deadline timers; Zephyr tickless idle does the rest.
- nRF52840: System ON idle with the radio stopped. The MAC uses `IEEE802154_CONFIG_RX_ON_WHEN_IDLE`.
- ESP32-C6/H2: `CONFIG_PM` and `CONFIG_IEEE802154_ESP32_SLEEP_ENABLE`. With sleep enabled the driver does not report `IEEE802154_RX_ON_WHEN_IDLE`, so the MAC opens the post-poll receive window itself: `start()`, wait up to macMaxFrameTotalWaitTime when the ACK had frame pending set, `stop()`.
- A zero-length data frame from the parent means no pending data; the device returns to sleep.
- Poll rate (BDB 6.8): at most 3 s between polls while a response is expected, during joining and during finding & binding. A parent holds a buffered frame for 7.5 s.
- An End Device Timeout Request follows every join and rejoin, with a timeout longer than the slow poll interval. The keepalive method comes from the parent information field of the response.
- Parent loss, after N consecutive failed polls or transmissions: secure rejoin on the current channel, secure rejoin on all channels, trust center rejoin. This also recovers from a channel change missed while asleep.

## 15. Extension points for coordinator and trust center

- Join, rejoin and leave are split into joiner-side and parent-side files sharing the frame codecs. Parent-side files are compiled with `zephyr_library_sources_ifdef(CONFIG_ZZB_ROUTER_CAPABLE ...)`. Small branches use `IS_ENABLED()`; functions contain no `#ifdef`.
- The parent has one "device joined" path. It sends APS Update Device to a remote trust center, or calls the local trust center through `node->tc` ops, which are NULL when `CONFIG_ZZB_TRUST_CENTER` is off. Children of the trust center itself never produce an Update Device frame on air. Received Request Key and Verify Key commands use the same dispatch.
- APS Tunnel is part of the router (milestone 16): a remote trust center reaches a joining grandchild through it.
- Public API tiers: (1) raw APSDE and endpoint registration with a raw handler; (2) ZDO client; (3) ZCL and BDB. An NCP build uses tiers 1 and 2 plus configuration get/set.
- Export and import of keys, frame counters and the device table are defined together with persistence, because zigpy relies on network backup and restore.
- NCP framing is independent of the transport (UART, USB CDC, TCP). Protocol candidates: ZBOSS NCP (zigpy-zboss exists), Z-Stack MT/ZNP (zigpy-znp exists), or a new protocol with a new zigpy radio library. The choice is made at milestone 20.

## 16. Reference code

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

## 17. Testing

- Unit tests: ztest on `native_sim/native/64`; CI also runs `native_sim` (32-bit) to catch pointer-size errors.
- Crypto and codec tests use R22 Annex C vectors, BDB 10.1 install code examples and frames captured from real networks. The first frames come from the Ziggurat and zigbee-rs test suites (listed in `NOTICE`) and from a test network whose network key may be published. MAC frames missing from these sources are built from the IEEE 802.15.4-2011 figures and R22 Tables D-1 to D-3.
- Multi-node integration tests: fake radio devices implementing `ieee802154_radio_api` (`tests/common/fake_radio.c`), connected by an in-process medium. Each fake radio is a net device for the custom-L2 backend with its own `net_if`, since the raw backend serves one port. The medium has a link matrix and filters by PAN ID and destination address; frame loss is added with the router MAC (milestone 15). It logs every frame with `LOG_HEXDUMP_DBG`. The MAC calls the radio API in the same way as on hardware. native_sim time is simulated, so timer behavior is deterministic.
- `tests/glue/raw` tests the radio port in raw mode against FFF fakes of the radio API: TX modes, software CSMA-CA, status mapping, frame pending from the ACK and the RX copy. `tests/glue/custom_l2` tests the custom-L2 backend with two fake radios.
- `tests/unit/mac` tests the device MAC in raw mode against FFF fakes of the radio API. The `tx()` fake plays the parent: it returns the ACK through `ieee802154_handle_ack()`, with frame pending when the test sets it, and injects replies with `net_recv_data()`. It simulates frame loss by withholding the ACK.
- `tests/unit/nwk` tests NWK with the MAC turned off, against FFF fakes of MCPS, MLME and the NLDE callbacks.
- `tests/build/radio` is a build-only consumer application. It enables the module on native_sim and, on nrf52840dk and ESP32-C6/H2, also the 802.15.4 driver in raw mode and NVS for the settings of NWK. A second scenario builds nrf52840dk with the custom-L2 backend. The application passes the `zephyr,ieee802154` chosen device to `zzb_node_init()` and defines the NLDE callbacks of NWK until APS defines them.
- CI runs on Woodpecker (`.woodpecker/build.yaml`): twister runs the tests on `native_sim` and `native_sim/native/64` and only builds the hardware targets.
- `tests/common/scripted_tc.c` plays the trust center side of join sequences captured with a sniffer from EZSP and Z-Stack coordinators on a test network with a known network key.
- Reboot and power-loss tests run on NVS over the flash simulator, kept in RAM with `--flash_in_ram` so that every run starts with erased flash; the node is destroyed and re-initialized. In the NWK tests a reboot is a second `zzb_nwk_init()` followed by loading the settings.
- A test asserts that no timer is armed while a sleepy node is idle.
- `tests/integration/nwk_data` runs two nodes on fake radios with the custom-L2 backend. Node 1 is a child of node 0; the test sets up both with NLME-SET and the neighbor table functions and sends APS frames built by hand.
- text2pcap and tshark decode the frame log of the medium and check MICs, as a decoder independent of the stack. The CI step `frame-log` runs after `native_sim` in the `antrea/tshark` image (Ubuntu 24.04, tshark 4.2.2), pinned by digest. `ci/check-frame-log.sh` takes the hexdumps of the medium from `handler.log`, builds a pcap with `text2pcap -l 230` (IEEE 802.15.4 without FCS) and decodes it with the test network key. The step fails if the log holds no frame or a frame with NWK security that tshark does not decrypt.
- Hardware interoperability: joining ZHA with EZSP and Z-Stack coordinators from milestone 11; current measurement from milestone 14.

## 18. Milestones

Each milestone is one feature branch with its tests.

1. Skeleton: `west.yml`, module files, Kconfig, CMake, CI with twister on native_sim and build-only nrf52840dk and ESP32-C6/H2 targets (`west blobs fetch hal_espressif`).
2. Common: buffer pool and metadata, stack work queue, request marshaling, timers, `zzb_node_init()`, EUI-64 provider.
3. Security primitives: MMO, HMAC-MMO, CCM level 5 with the level-field rule, auxiliary header, install code.
4. MAC frame codec: data, beacon and command frames.
5. Radio port: fake radio and medium, raw and custom-L2 shims, TX modes and error mapping, frame pending from the ACK.
6. MAC device side: active scan, association with response polling, data poll, software retries, filters, binding of the radio port to the node, EUI-64 from the custom-L2 link address.
7. NWK frame codec (header, the 13 commands of R22 3.4, beacon payload) and security, PSA key cache, NIB, neighbor table, frame counter persistence, NLDE-DATA for end devices, MLME-SET of macPANId and macShortAddress, tshark check of the frame log in CI.
8. NWK join, secure rejoin, trust center rejoin, leave (with rejoin flag), End Device Timeout Request, network status, address conflict.
9. APS: APSDE, ACK, retries with a new frame counter, duplicate rejection, link-key security, Transport Key, Request Key, Verify Key, Confirm Key, Switch Key, key-pair table.
10. ZDO server (descriptors, IEEE_addr/NWK_addr, Device_annce, Mgmt_Leave, Bind/Unbind, Mgmt_Lqi, Mgmt_Bind, Mgmt_NWK_Update channel change) and Node_Desc_req client. The Node Descriptor server mask reports stack compliance revision 22: the TCLK exchange sends Request Key only after Node_Desc_rsp from the trust center reports revision 21 or later (BDB 10.2.5).
11. BDB end device: initialization and boot rejoin (7.1), steering of a node not on a network (8.2), TCLK exchange (10.2.5), resets (chapter 9), parent-loss rejoin. Interop: join ZHA on EZSP and Z-Stack and stay joined across reboot.
12. ZCL foundation (read, write, discover attributes, default response), attribute macros, Basic, Identify, On/Off servers. Interop: ZHA creates the entity.
13. Attribute reporting with persistence; finding & binding (BDB 8.4, 8.5).
14. Sleepy end device: poll scheduler with holds, radio off between polls, timeout negotiation, Power Configuration and Poll Control clusters.
15. Router, part 1: MAC coordinator functions (beacons, association response, indirect queue, frame-pending table), stochastic address allocation, child aging and End Device Timeout server, permit join, Parent_annce; esp32 driver patch; frame loss in the fake medium.
16. Router, part 2: route discovery, link status, broadcast transaction table with passive ACK, many-to-one routes, route record, source-route relay, APS Update Device and Tunnel.
17. OTA Upgrade client with MCUboot (`flash_img`, `boot_request_upgrade()`).
18. Coordinator: network formation (energy scan, PAN ID selection), concentrator and source route table, network manager.
19. Trust center (BDB 10.3): key generation with `psa_generate_random()`, join and rejoin policy, network key update and switch, per-entry storage.
20. NCP; Green Power proxy basic.

## 19. Risks

| Risk | Mitigation |
|---|---|
| The stack passes its own tests but fails with real coordinators | ZHA interop from milestone 11; fixtures from sniffer captures; independent decoder in CI |
| esp32 driver: no frame-pending control, no custom-L2 branch, no CCA-only TX mode, `tx()` returns 0 after CCA failure and missing ACK, RX_ON_WHEN_IDLE withheld with sleep enabled, binary blobs | raw mode, software retries, zero-length frame fallback, ACK detected through `ieee802154_handle_ack()`, upstream patch, blob fetch in CI |
| Frame counter or persistence loss: the device cannot rejoin, or reports stop | watermark key outside the factory-reset subtree; reboot and power-loss tests; one settings key per record |
| Router and coordinator scope | ZigBee PRO/2007 layer PICS (08-0006-05) as a per-milestone checklist; features outside the ZHA path deferred |
| Sleepy timing and current are not observable on native_sim | driver RX_ON_WHEN_IDLE where available; deadline-only timers with an idle test; current measurement on hardware |

New dependencies accepted with this document: PSA Crypto (Zephyr mbedTLS / TF-PSA-Crypto), settings with NVS or ZMS, FFF, `net_buf`. The `antrea/tshark` container image is accepted for the frame log check (milestone 7). Zephyr shell and MCUboot are confirmed at the milestones that introduce them.
