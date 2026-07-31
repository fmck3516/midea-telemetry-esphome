## UART port comparison (Midea AC LAN)

The [Midea AC LAN](https://github.com/wuwentao/midea_ac_lan) integration sources a partially overlapping set of sensor data from the UART port located in the IDU. Tested using a MRCOOL DIY 12K 4th Gen 115V and the dongle that ships with a Midea U.

The UART port is more easily accessible than the diagnostic port on the ODU, which makes it attractive. Unfortunately, the data that is available seems to be highly model-dependent and too spotty to pursue this route further:

| Sensor | Diagnostic Port | UART | Comment |
|---|---|---|---|
| `indoor_ambient_temperature`  | ✅ | ✅ | |
| `indoor_coil_temperature`     | ✅ | ✅ | |
| `outdoor_ambient_temperature` | ✅ | ✅ | |
| `outdoor_coil_temperature`    | ✅ | ✅ | |
| `discharge_temperature`       | ✅ | ❌ | |
| `ipm_temperature`             | ❌ | ❌ | |
| `operating_mode`              | ✅ | ✅ | |
| `compressor_frequency_target` | ✅ | ❌ | |
| `compressor_frequency_actual` | ✅ | ✅ | |
| `outdoor_fan_speed`           | ✅ | ❌ | |
| `eev_steps`                   | ✅ | ❌ | |
| `indoor_setpoint`             | ✅ | ✅ | |
| `input_voltage`               | ✅ | ❌ | |
| `current_draw`                | ✅ | ❌ | |
| `dc_bus_voltage`              | ✅ | ❌ | |
| `compressor_voltage`          | ❔ | ✅ | candidate: 0x05 / byte 2 |
| `indoor_fan_speed_target`     | ❌ | ✅ | |
| `indoor_fan_speed_actual`     | ❌ | ✅ | |