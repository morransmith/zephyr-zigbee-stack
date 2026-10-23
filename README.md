<div align="center">

# zzb

**Zigbee 3.0 stack for Zephyr RTOS**

![Release](https://img.shields.io/github/v/tag/morransmith/zephyr-zigbee-stack?sort=semver&label=release&style=flat-square&labelColor=2b2f36&color=3b82c4)
![Zigbee PRO R22](https://img.shields.io/badge/Zigbee_PRO-R22-0e7c86?style=flat-square&labelColor=2b2f36)
![BDB 3.0.1](https://img.shields.io/badge/BDB-3.0.1-0e7c86?style=flat-square&labelColor=2b2f36)
![ZCL rev 8](https://img.shields.io/badge/ZCL-rev_8-0e7c86?style=flat-square&labelColor=2b2f36)
![Zephyr v4.4.2](https://img.shields.io/badge/Zephyr-v4.4.2-6f42c1?style=flat-square&labelColor=2b2f36)
![License Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-3b82c4?style=flat-square&labelColor=2b2f36)

[Overview](#overview) · [Getting started](#getting-started) · [Features](#features) · [Samples](#samples) · [Documentation](#documentation) · [License](#license)

</div>

---

## Overview

zzb implements the Zigbee 3.0 protocol layers on top of the IEEE 802.15.4 radio drivers of Zephyr: MAC, NWK, APS, ZDO, Base Device Behavior and the Zigbee Cluster Library. It is a Zephyr module. A firmware adds it to its west manifest, enables it in Kconfig and describes its device as endpoints with clusters.

Zephyr has no Zigbee stack of its own. The existing stacks come with vendor SDKs, as binary libraries or under licenses limited to the vendor's chips. zzb is written from the specifications, uses only Zephyr APIs and is licensed under Apache-2.0, so one firmware source builds for chips of different vendors. Its interoperability target is Home Assistant ZHA.

## Getting started

### Requirements

- Zephyr v4.4.2 with the Zephyr SDK. Other Zephyr versions are not tested.
- A settings backend (NVS or ZMS) on a flash partition. The stack keeps the network state, keys, frame counters and attributes there.
- ESP32 targets: the Espressif binary blobs.

### Standalone workspace

```sh
west init -m https://github.com/morransmith/zephyr-zigbee-stack --mr master zzb-ws
cd zzb-ws
west update
west blobs fetch hal_espressif    # ESP32 targets only
```

### Adding zzb to a firmware

West manifest of the firmware:

```yaml
manifest:
  remotes:
    - name: zzb
      url-base: https://github.com/morransmith
  projects:
    - name: zephyr-zigbee-stack
      remote: zzb
      revision: master
      import: true    # Zephyr v4.4.2 and the modules zzb needs
```

`master` is the latest release. To fix the version, set `revision` (or `--mr`) to the tag of the needed release.

To build with another Zephyr version, the firmware declares `zephyr` in its own manifest. A project defined in the manifest of the firmware takes precedence over an imported project with the same name, so west ignores the `zephyr` entry of zzb together with its `name-allowlist`, and the firmware lists the modules itself:

```yaml
manifest:
  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos
    - name: zzb
      url-base: https://github.com/morransmith
  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: v4.5.0-rc2
      import:
        name-allowlist:
          - cmsis_6
          - hal_nordic
          - hal_espressif
          - mbedtls
          - mcuboot
          - tf-psa-crypto
    - name: zephyr-zigbee-stack
      remote: zzb
      revision: master
      import: true
```

`prj.conf`:

```ini
CONFIG_ZZB=y
CONFIG_NETWORKING=y
CONFIG_IEEE802154=y
CONFIG_IEEE802154_RAW_MODE=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
```

For OTA Upgrade, build with sysbuild and MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y`) and enable `CONFIG_ZZB_ZCL_OTA`. `scripts/zigbee_ota.py` turns `zephyr.signed.bin` into a Zigbee OTA file that ZHA can install.

With Zephyr power management (`CONFIG_PM`) the stack holds a lock of all PM states while the radio transmits or receives, so a sleepy end device sleeps between its polls. On ESP32-H2 also enable `CONFIG_PM_DEVICE` and `CONFIG_COUNTER` and make the RTC timer a wakeup source in the board devicetree:

```dts
&rtc_timer {
	status = "okay";
	wakeup-source;
};
```

and keep the radio powered in light sleep before `zzb_node_init()`; otherwise the HAL powers it down and the node finds no network:

```c
#include <esp_sleep.h>

esp_sleep_pd_config(ESP_PD_DOMAIN_MODEM, ESP_PD_OPTION_ON);
```

### Build and test

```sh
west build -b esp32h2_devkitm/esp32h2 samples/light -- -DCONFIG_SAMPLE_DIMMABLE_LIGHT=y
west flash
west twister -T tests -p native_sim -p native_sim/native/64
```

## Features

### Roles

| Role | Status |
|---|---|
| End device | Supported |
| Sleepy end device | Supported |
| Router | Planned |
| Coordinator | Planned |
| Trust center | Planned |
| Network co-processor | Planned |

### Stack

| Layer | Supported |
|---|---|
| MAC | Device side of IEEE 802.15.4-2011: active scan, association, data poll, retries |
| NWK | Network discovery, join, secure and trust center rejoin, leave, end device timeout, parent loss recovery |
| APS | Acknowledged delivery, duplicate rejection, binding and group tables, link key security |
| Security | Centralized security, install codes, trust center link key exchange, PSA Crypto |
| ZDO | Discovery, binding and management requests, channel change |
| BDB | Initialization, network steering, finding & binding, resets, rejoin after parent loss |
| ZCL | Foundation commands, attribute reporting, groups, scenes, persistent attributes |
| OTA | OTA Upgrade client with MCUboot |

### Clusters

| Domain | Clusters |
|---|---|
| General | Basic, Power Configuration, Device Temperature Configuration, Identify, Groups, Scenes, On/Off, On/Off Switch Configuration, Level Control, Alarms, Time, Binary Input, Poll Control, OTA Upgrade |
| Lighting | Color Control, Ballast Configuration |
| Measurement and sensing | Illuminance Measurement, Illuminance Level Sensing, Temperature, Pressure, Flow and Relative Humidity Measurement, Occupancy Sensing |
| Closures | Window Covering, Door Lock |
| HVAC | Thermostat, Thermostat User Interface Configuration, Fan Control, Pump Configuration and Control |
| Security and safety | IAS Zone, IAS WD, IAS ACE |
| Energy | Metering, Meter Identification, Electrical Measurement |
| Appliances | Power Profile, Appliance Control, Appliance Identification, Appliance Events and Alerts, Appliance Statistics |

OTA Upgrade has a client only. Basic, Power Configuration, Device Temperature Configuration, On/Off Switch Configuration, Binary Input, Ballast Configuration and Thermostat User Interface Configuration have a server only. The other clusters have both.

Device types: all types of the Device Type Library rev 2 except the Range Extender, which needs the router role.

### Not supported

- Router, coordinator, trust center and network co-processor roles (planned).
- Zigbee R23 features, Sub-GHz, touchlink, Green Power, Zigbee Direct.
- APS fragmentation and distributed security.
- Low-power mode of the ESP32 radio: the driver in Zephyr v4.4.2 lacks it, so the radio PHY of a sleepy ESP32 device stays powered.
- EmberZNet (EZSP) coordinators are not checked yet.

### Hardware

| Board | Status |
|---|---|
| `esp32h2_devkitm/esp32h2` | Built in CI, checked on hardware with ZHA and a Z-Stack coordinator |
| `esp32c6_devkitc/esp32c6/hpcore` | Built in CI |
| `nrf52840dk/nrf52840` | Built in CI |
| `native_sim`, `native_sim/native/64` | Unit and integration tests in CI |

Other boards need an IEEE 802.15.4 driver in Zephyr with raw mode or the custom L2 interface.

## Samples

[`samples/`](samples/README.md) holds a sample for every end device type of the Device Type Library and four multi-endpoint devices: a dual relay, a multisensor, a door lock with a door contact and a power strip. The samples build for the three hardware boards above and join a network with a press of the board button.

## Documentation

- **API reference:** `doxygen doc/Doxyfile`, output in `doc/build/html`.
- **Design:** [`doc/architecture.md`](doc/architecture.md).
- **Samples:** [`samples/README.md`](samples/README.md).

## License

zzb is licensed under the [Apache License 2.0](LICENSE). Test data taken from other projects is listed in [`NOTICE`](NOTICE).
