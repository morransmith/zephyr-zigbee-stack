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

### controller

| Kconfig | Device type | ID | Clusters besides Basic, Identify and the Identify client |
|---|---|---|---|
| `CONFIG_SAMPLE_SCENE_SELECTOR` | Scene Selector | 0x0004 | Power Configuration; Groups, Scenes clients |
| `CONFIG_SAMPLE_CONFIGURATION_TOOL` | Configuration Tool | 0x0005 | Groups client |
| `CONFIG_SAMPLE_REMOTE_CONTROL` | Remote Control | 0x0006 | Power Configuration; Groups, On/Off, Level Control, Color Control clients |
| `CONFIG_SAMPLE_COMBINED_INTERFACE` | Combined Interface | 0x0007 | Groups, Scenes, On/Off, Level Control, Color Control clients |
| `CONFIG_SAMPLE_COLOR_CONTROLLER` | Color Controller | 0x0800 | Power Configuration; Groups, On/Off, Level Control, Color Control clients |
| `CONFIG_SAMPLE_COLOR_SCENE_CONTROLLER` | Color Scene Controller | 0x0810 | Power Configuration; Groups, Scenes, On/Off, Level Control, Color Control clients |
| `CONFIG_SAMPLE_NON_COLOR_CONTROLLER` | Non-Color Controller | 0x0820 | Power Configuration; Groups, On/Off, Level Control clients |
| `CONFIG_SAMPLE_NON_COLOR_SCENE_CONTROLLER` | Non-Color Scene Controller | 0x0830 | Power Configuration; Groups, Scenes, On/Off, Level Control clients |
| `CONFIG_SAMPLE_CONTROL_BRIDGE` | Control Bridge | 0x0840 | Groups, Scenes, On/Off, Level Control, Color Control, Illuminance Measurement, Illuminance Level Sensing, Occupancy Sensing clients |
| `CONFIG_SAMPLE_ON_OFF_SENSOR` | On/Off Sensor | 0x0850 | Power Configuration; On/Off client |

A short press sends the next command of a cycle to the bound destinations: Toggle, Move to Level with On/Off and Move to Color Temperature, as far as the device type has the clients, in two rounds, with level 50 and 250 mireds in the first and level 200 and 400 mireds in the second. The scene controllers toggle only in the first round, store scene 1 of group 0 at its end and recall it at the end of the second; the Scene Selector recalls scenes 1 and 2 of group 0 in turn, and the Configuration Tool adds group 0x0001 and removes it in turn. led0 blinks while the device identifies itself. Finding & binding: initiator with unicast bindings. The device types with Power Configuration are sleepy end devices.

## Deviations from the DTL

- The Range Extender (0x0008) has no sample, since it is a router.
