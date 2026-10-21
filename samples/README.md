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

### covering

| Kconfig | Device type | ID | Clusters besides Basic, Identify |
|---|---|---|---|
| `CONFIG_SAMPLE_WINDOW_COVERING` | Window Covering | 0x0202 | Groups, Scenes, Window Covering (tilt blind, lift and tilt) |
| `CONFIG_SAMPLE_WINDOW_COVERING_CONTROLLER` | Window Covering Controller | 0x0203 | Power Configuration; clients Identify, Groups, Scenes, Window Covering |

The Window Covering moves lift and tilt by 10 % a second to the targets of Up/Open, Down/Close, Go To Lift Percentage, Go To Tilt Percentage and recalled scenes, and Stop stops it; led0 is on while it moves. A short press runs Up/Open, Stop, Down/Close and Stop in turn, on the motor of the Window Covering and as commands to the bindings of the controller. Finding & binding: target for the Window Covering, initiator for the controller. The controller is a sleepy end device.

### hvac

| Kconfig | Device type | ID | Clusters besides Basic, Identify, Fan Control |
|---|---|---|---|
| `CONFIG_SAMPLE_THERMOSTAT` | Thermostat | 0x0301 | Thermostat, Thermostat User Interface Configuration; client Identify |
| `CONFIG_SAMPLE_HEATING_COOLING_UNIT` | Heating/Cooling Unit | 0x0300 | Groups, On/Off; client Thermostat |
| `CONFIG_SAMPLE_AIR_PURIFIER` | Air Purifier | 0x0308 | Groups |

The Thermostat starts at 21.00 °C, and every minute LocalTemperature moves by 0.10 °C toward OccupiedHeatingSetpoint in Heat, toward OccupiedCoolingSetpoint in Cool and toward 18.00 °C in the other modes; until it reaches the setpoint, PIHeatingDemand or PICoolingDemand is 100 % and ThermostatRunningState shows heating or cooling. The weekly schedule is not carried out. A short press switches SystemMode between Off and Heat on the Thermostat, toggles OnOff on the Heating/Cooling Unit and selects the next FanMode of Off, Low, Medium, High and Auto on the Air Purifier; led0 is on while SystemMode, OnOff or FanMode is not off. Finding & binding: target.

## Deviations from the DTL

- The Range Extender (0x0008) has no sample, since it is a router.
