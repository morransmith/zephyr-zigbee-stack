# Samples

Each sample is an end device for nrf52840dk/nrf52840, esp32c6_devkitc/esp32c6/hpcore or esp32h2_devkitm/esp32h2. A family sample builds one device type of the Device Type Library, selected with a Kconfig choice; a combination sample has several device types on the endpoints of one node. The devices join with network steering (BDB 8.2) and the default trust center link key.

## Building

```
west build -b esp32h2_devkitm/esp32h2 samples/light -- -DCONFIG_SAMPLE_DIMMABLE_LIGHT=y
west twister -T samples --build-only -p nrf52840dk/nrf52840
```

Without an option a family sample builds the first device type of its table. `sample.yaml` has one scenario per device type.

## Button and LED

| Button sw0 | Action |
|---|---|
| Short press off a network | network steering |
| Short press on a network | the action of the device type |
| Hold of 3 s to 10 s | finding & binding as target or initiator (BDB 8.4, 8.5) |
| Hold of 10 s or longer | `zzb_bdb_reset()`: the node leaves its network and restores its defaults |

A node that is off a network at boot or has left its network starts network steering. led0 shows the state of the device and blinks while the device identifies itself; nrf52840dk has it, the ESP32 devkits do not. The console shows the BDB events and the values that change.

Sensors produce synthetic values. The device types that run on a battery are sleepy end devices whose Power Configuration server reports 3.0 V and 100 %.

## Families

### light

| Kconfig | Device type | ID | Clusters besides Basic, Identify, Groups, Scenes, On/Off |
|---|---|---|---|
| `CONFIG_SAMPLE_ON_OFF_LIGHT` | On/Off Light | 0x0100 | |
| `CONFIG_SAMPLE_DIMMABLE_LIGHT` | Dimmable Light | 0x0101 | Level Control |
| `CONFIG_SAMPLE_COLOR_DIMMABLE_LIGHT` | Color Dimmable Light | 0x0102 | Level Control, Color Control with XY |
| `CONFIG_SAMPLE_COLOR_TEMPERATURE_LIGHT` | Color Temperature Light | 0x010c | Level Control, Color Control with color temperature |
| `CONFIG_SAMPLE_EXTENDED_COLOR_LIGHT` | Extended Color Light | 0x010d | Level Control, Color Control with XY and color temperature |

A short press toggles OnOff, and led0 shows it. The level and the color go to the console. Finding & binding: target.

### ias

| Kconfig | Device type | ID | Clusters besides Basic, Identify |
|---|---|---|---|
| `CONFIG_SAMPLE_IAS_ZONE` | IAS Zone | 0x0402 | Power Configuration, IAS Zone (contact switch) |
| `CONFIG_SAMPLE_IAS_WARNING_DEVICE` | IAS Warning Device | 0x0403 | Groups, IAS Zone (standard warning device), IAS WD |
| `CONFIG_SAMPLE_IAS_ACE` | IAS Ancillary Control Equipment | 0x0401 | Power Configuration, IAS Zone (keypad); clients Identify, IAS ACE |
| `CONFIG_SAMPLE_IAS_CIE` | IAS Control and Indication Equipment | 0x0400 | IAS ACE; clients Identify, IAS Zone, IAS WD |

On the IAS Zone and the IAS Warning Device a short press inverts Alarm1 of ZoneStatus, and led0 shows it; led0 of the IAS Warning Device blinks while a warning runs. The IAS ACE sends Arm with Arm All Zones and with Disarm in turn, with the code 1234, and led0 shows whether the panel is armed. The IAS CIE answers Arm, Get Panel Status and Zone Enroll Request, sends Start Warning to the bound warning devices when a zone reports an alarm while the panel is armed, and a short press arms or disarms the panel, which led0 shows. Finding & binding: target, which only identifies, since the DTL forbids it for the IAS clusters. The IAS Zone and the IAS ACE are sleepy end devices.

### door_lock

| Kconfig | Device type | ID | Clusters besides Basic, Identify |
|---|---|---|---|
| `CONFIG_SAMPLE_DOOR_LOCK` | Door Lock | 0x000a | Alarms, Door Lock; client Time |
| `CONFIG_SAMPLE_DOOR_LOCK_CONTROLLER` | Door Lock Controller | 0x000b | Power Configuration; clients Identify, Door Lock |

led0 of the Door Lock is the bolt, on while locked: Lock Door and Unlock Door move it, and a short press locks or unlocks by hand. A short press of the Door Lock Controller sends Lock Door and Unlock Door in turn, without a code. Finding & binding: target on the Door Lock, initiator with unicast bindings on the Door Lock Controller, which is a sleepy end device.

## Deviations from the DTL

- The Range Extender (0x0008) has no sample, since it is a router.
