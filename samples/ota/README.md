# ota

An On/Off Light with an OTA Upgrade client (ZCL 11). Home Assistant with ZHA installs a new firmware on it over Zigbee. MCUboot boots the firmware, so the sample is built with sysbuild.

| Endpoint | Device type | ID | Clusters |
|---|---|---|---|
| 1 | On/Off Light | 0x0100 | Basic, Identify, Groups, Scenes, On/Off; client OTA Upgrade |

The button and the LED work as in the other samples ([../README.md](../README.md)): a short press toggles OnOff, and led0 shows it.

## Build and flash

```
west build --sysbuild -b esp32h2_devkitm/esp32h2 -d build-ota samples/ota
west flash -d build-ota
```

`west flash` writes MCUboot and the application. On ESP32 boards it takes the serial port with `--esp-device <port>`.

The firmware version is in `VERSION`, 1.0.0. The OTA client reports it as CurrentFileVersion, which is APPVERSION: `major << 24 | minor << 16 | patchlevel << 8 | tweak`, 0x01000000 for 1.0.0. The manufacturer code and the image type are `CONFIG_SAMPLE_MANUFACTURER_CODE` and `CONFIG_SAMPLE_OTA_IMAGE_TYPE`, 0x0000 by default, and are set with e.g. `-- -DCONFIG_SAMPLE_MANUFACTURER_CODE=0x1234`. The OTA files of the device must carry the same values.

## Home Assistant

ZHA takes OTA files from the index file of a `zigpy_local` provider. Add the provider to `configuration.yaml`, where `/config` is the configuration directory of Home Assistant:

```yaml
zha:
  zigpy_config:
    ota:
      extra_providers:
        - type: zigpy_local
          index_file: /config/zzb-ota/index.json
```

Restart Home Assistant. Then join the device: Settings → Devices & services → Zigbee Home Automation → Add device. The sample starts network steering at boot when it is off a network, and at a short press of sw0. The page of the device has the update entity Firmware with the installed version 0x01000000.

## Update

1. Raise the version in `samples/ota/VERSION`, for example `VERSION_MINOR = 1` for 1.1.0 (0x01010000), and build it into another directory:

   ```
   west build --sysbuild -b esp32h2_devkitm/esp32h2 -d build-ota-2 samples/ota
   ```

2. Build the OTA file and its entry of the index file. `<config>` is the configuration directory of Home Assistant on the host:

   ```
   mkdir -p <config>/zzb-ota
   python3 scripts/zigbee_ota.py --manufacturer 0x0000 --image-type 0x0000 \
       --file-version 0x01010000 --index <config>/zzb-ota/index.json \
       build-ota-2/ota/zephyr/zephyr.signed.bin <config>/zzb-ota/ota-1.1.0.zigbee
   ```

   The script creates `index.json` if it is missing and replaces an entry with the same manufacturer code, image type and file version.

3. Restart Home Assistant. ZHA reads the index file at the first check after the start and keeps it for 24 h. It also keeps the last Query Next Image Request of the device, which the device sends after the Image Notify of ZHA at its join or rejoin, and checks it against the index at the start, so the Firmware entity offers 0x01010000.

4. When the entity offers no update, let the device query: in Developer tools → Actions, run `homeassistant.update_entity` on the Firmware entity of the device. ZHA broadcasts Image Notify, and the device sends Query Next Image Request. Without this step ZHA broadcasts Image Notify 3.9 h after the start and every 3.9 h after that.

5. Press Install on the Firmware entity. The device downloads the file in blocks of 48 octets at about 0.8 KB/s, so the 274 KB image of ESP32-H2 takes about 5.5 min. The console shows ImageUpgradeStatus 1 (downloading), 2 (download complete) and 4 (count down). One second after the Upgrade End Response the device reboots, MCUboot installs the image, and the node rejoins its network. The new firmware confirms its image when BDB initialization ends with success, and ZHA shows the installed version 0x01010000.

## MCUboot

| Board | MCUboot mode | Image check |
|---|---|---|
| nrf52840dk/nrf52840 | swap using offset: an image that is not confirmed is reverted at the next reset | RSA signature with `root-rsa-2048.pem` of the mcuboot module |
| esp32c6_devkitc/esp32c6/hpcore, esp32h2_devkitm/esp32h2 | overwrite-only: the image is not reverted, so a firmware is tested before it is offered | hash only |

The key of the mcuboot module is public. A product sets its own with `SB_CONFIG_BOOT_SIGNATURE_TYPE_*` and `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` in `sysbuild.conf`.
