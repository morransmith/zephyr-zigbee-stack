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

### output

| Kconfig | Device type | ID | Clusters besides Basic, Identify, Groups, Scenes, On/Off |
|---|---|---|---|
| `CONFIG_SAMPLE_ON_OFF_OUTPUT` | On/Off Output | 0x0002 | |
| `CONFIG_SAMPLE_LEVEL_CONTROLLABLE_OUTPUT` | Level Controllable Output | 0x0003 | Level Control |
| `CONFIG_SAMPLE_MAINS_POWER_OUTLET` | Mains Power Outlet | 0x0009 | |
| `CONFIG_SAMPLE_ON_OFF_PLUG_IN_UNIT` | On/Off Plug-in Unit | 0x010a | Level Control |
| `CONFIG_SAMPLE_DIMMABLE_PLUG_IN_UNIT` | Dimmable Plug-In Unit | 0x010b | Level Control |

A short press toggles OnOff, and led0 shows the relay. The level goes to the console. Finding & binding: target.

### ballast

| Kconfig | Device type | ID |
|---|---|---|
| `CONFIG_SAMPLE_ON_OFF_BALLAST` | On/Off Ballast | 0x0108 |
| `CONFIG_SAMPLE_DIMMABLE_BALLAST` | Dimmable Ballast | 0x0109 |

Both types have the Basic, Power Configuration, Device Temperature Configuration, Identify, Groups, Scenes, On/Off, Level Control, Alarms, Ballast Configuration and Illuminance Level Sensing servers and the Illuminance Measurement and Occupancy Sensing clients. The On/Off Ballast has Level Control for the commands with On/Off that a group with dimmable lights receives (DTL 29.3.1).

A short press toggles OnOff, and led0 shows it. The level goes to the console, and every hour LampBurnHours grows by 1 when the lamp is on. Finding & binding: target.

### switch

| Kconfig | Device type | ID | Clusters besides Basic, Power Configuration, Identify, On/Off Switch Configuration, Identify client, On/Off client |
|---|---|---|---|
| `CONFIG_SAMPLE_ON_OFF_SWITCH` | On/Off Switch | 0x0000 | |
| `CONFIG_SAMPLE_LEVEL_CONTROL_SWITCH` | Level Control Switch | 0x0001 | Level Control client |
| `CONFIG_SAMPLE_ON_OFF_LIGHT_SWITCH` | On/Off Light Switch | 0x0103 | |
| `CONFIG_SAMPLE_DIMMER_SWITCH` | Dimmer Switch | 0x0104 | Level Control client |
| `CONFIG_SAMPLE_COLOR_DIMMER_SWITCH` | Color Dimmer Switch | 0x0105 | Level Control client, Color Control client |

A short press sends the next command of the cycle On/Off, Move to Level with On/Off (level 50 or 200), Move to Color Temperature (250 or 400 mireds) to the bound destinations; the cycle holds only the commands of the clients of the type. The On/Off command is Toggle when SwitchActions is 0x02, otherwise On and Off in turn, in the order that SwitchActions names. Finding & binding: initiator with unicast bindings. Sleepy end device.

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

### sensor

| Kconfig | Device type | ID | Clusters besides Basic, Power Configuration, Identify and the Identify client |
|---|---|---|---|
| `CONFIG_SAMPLE_SIMPLE_SENSOR` | Simple Sensor | 0x000c | Binary Input (Basic) |
| `CONFIG_SAMPLE_LIGHT_SENSOR` | Light Sensor | 0x0106 | Illuminance Measurement |
| `CONFIG_SAMPLE_OCCUPANCY_SENSOR` | Occupancy Sensor | 0x0107 | Occupancy Sensing |
| `CONFIG_SAMPLE_LIGHT_LEVEL_SENSOR` | Light Level Sensor | 0x010e | Illuminance Level Sensing |
| `CONFIG_SAMPLE_TEMPERATURE_SENSOR` | Temperature Sensor | 0x0302 | Temperature Measurement |
| `CONFIG_SAMPLE_PRESSURE_SENSOR` | Pressure Sensor | 0x0305 | Pressure Measurement |
| `CONFIG_SAMPLE_FLOW_SENSOR` | Flow Sensor | 0x0306 | Flow Measurement |

The Simple Sensor, the Light Level Sensor, the Pressure Sensor and the Flow Sensor are not certifiable, since their sensing clusters are not (DTL Annexes C, D, G and H). A short press inverts PresentValue of the Simple Sensor while OutOfService is FALSE, and sets Occupancy of the Occupancy Sensor, which returns to 0 30 s after the last press; led0 shows the value. The other device types set the next value of a fixed sequence at the start, every 60 s and at a short press: 1 lx to 100 klx in factors of 10, 20.00 to 24.00 degC, 1000 to 1020 hPa and 1.0 to 3.0 m3/h, with MinMeasuredValue and MaxMeasuredValue at the ends of the sequence. The Light Level Sensor sets LevelStatus from the illuminance and IlluminanceTargetLevel with a dead band of 1000 on each side, and on target while IlluminanceTargetLevel is 0xffff. Finding & binding: initiator with unicast bindings. Sleepy end device.

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

### pump

| Kconfig | Device type | ID | Clusters besides Basic, Identify, client Identify |
|---|---|---|---|
| `CONFIG_SAMPLE_PUMP` | Pump | 0x0303 | Groups, Scenes, On/Off, Pump Configuration and Control, Pressure Measurement, Flow Measurement |
| `CONFIG_SAMPLE_PUMP_CONTROLLER` | Pump Controller | 0x0304 | Power Configuration; clients On/Off, Pump Configuration and Control, Temperature, Pressure and Flow Measurement |

A short press toggles OnOff of the Pump, and led0 shows it; the controller sends Toggle to its bindings. While the Pump is on, it measures 200.0 kPa and 2.0 m³/h and sets the Running bit of PumpStatus, and all three are 0 while it is off. Finding & binding: target for the Pump, initiator for the controller. The controller is a sleepy end device. Neither device type is certifiable, since the Pump Configuration and Control cluster is not (DTL Annexes E and F).

## Combinations

### dual_relay

| Endpoint | Device type | ID | Clusters |
|---|---|---|---|
| 1 | On/Off Light | 0x0100 | Basic, Identify, Groups, Scenes, On/Off |
| 2 | On/Off Light | 0x0100 | Identify, Groups, Scenes, On/Off |
| 3 | On/Off Light Switch | 0x0103 | Identify; clients Identify, On/Off |

Two relays and a wall switch. Short presses toggle relay 1, relay 2 and the lights bound to endpoint 3 in turn. led0 shows relay 1 and led1 relay 2. Finding & binding: initiator on endpoint 3.

### multisensor

| Endpoint | Device type | ID | Clusters |
|---|---|---|---|
| 1 | Occupancy Sensor | 0x0107 | Basic, Power Configuration, Identify, Occupancy Sensing; client Identify |
| 2 | Light Sensor | 0x0106 | Identify, Illuminance Measurement; client Identify |
| 3 | Temperature Sensor | 0x0302 | Identify, Temperature Measurement, Relative Humidity Measurement; client Identify |

A battery-powered motion sensor, a sleepy end device. A short press sets Occupancy, which returns to 0 30 s after the last press; led0 shows it. Illuminance, temperature and humidity take the next value of a fixed sequence at the start and every 60 s: 1 lx to 10 klx in factors of 10, 20.00 to 24.00 degC and 40 to 60 %. Finding & binding: initiator on endpoint 1.

## Deviations from the DTL

- The Range Extender (0x0008) has no sample, since it is a router.
- The Level Controllable Output (0x0003) has the Level Control for Lighting server of the stack in place of the general Level Control cluster that the DTL asks for (ZCL 3.10): CurrentLevel is 1 to 254, and MinLevel, MaxLevel and the other attributes of the general cluster are absent.
