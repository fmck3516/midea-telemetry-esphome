# midea-telemetry-esphome

ESPHome component to feed telemetry from Midea's diagnostic port into Home Assistant

An [ESPHome external component](https://esphome.io/components/external_components/) that emulates Midea's inverter tester on the ODU diagnostic port and exposes all currently known telemetry fields as Home Assistant sensors. The protocol reverse engineering, Arduino sketches, schematic, and analysis tooling live in [midea-telemetry](https://github.com/fmck3516/midea-telemetry).

It drives the two-wire bus exactly like [inverter-tester-emulator.ino](https://github.com/fmck3516/midea-telemetry/blob/main/arduino/inverter-tester-emulator/inverter-tester-emulator.ino) — same wake-up sequence, request table, and timing — but from a dedicated FreeRTOS task, so ESPHome's main loop (Wi-Fi, API) is never blocked by the ~380 ms bit-banged frame cycles. The bus is polled continuously; `update_interval` only controls how often the latest values are published. If the ODU stops answering for 60 s, the sensors report "unavailable" instead of freezing on the last value.

Hardware is the same as for the Arduino sketches (ESP32 + level shifter, see the [schematic](https://github.com/fmck3516/midea-telemetry/tree/main/schematics)). ESP32 only — the component needs FreeRTOS and a second core.

> ⚠️ **Safety.** The outdoor unit runs on mains voltage and can retain a dangerous charge after being unplugged. Only plug a connector into the diagnostic port if you know what you are doing. You are responsible for your own hardware and safety.

## Example configuration

See [example-config/device.yaml](example-config/device.yaml) for a complete, flashable configuration with all 13 sensors. The short version:

```yaml
esphome:
  name: midea-telemetry

esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: arduino

external_components:
  - source: github://fmck3516/midea-telemetry-esphome   # or, for a local checkout:
    components: [midea_telemetry]                       # - source: components

midea_telemetry:
  clk_pin: GPIO3   # D2 on the XIAO ESP32S3
  dat_pin: GPIO2   # D1
  update_interval: 10s

sensor:
  - platform: midea_telemetry
    outdoor_coil_temperature:
      name: Outdoor coil temperature
    compressor_frequency_actual:
      name: Compressor frequency (actual)
    # ... every field from the table below is available
```

Every sensor is optional — list only the ones you want as entities.

## Fields

Byte mapping and conversion formulas as documented in [Reverse Engineering Midea's ODU Diagnostic Port](https://medium.com/@florian.mckee/reverse-engineering-mideas-odu-diagnostic-port-af603e159053); they match `tools/live_values.py` and the dashboard in the main repo.

| Sensor | Unit | Response type | Bytes |
|---|---|---|---|
| `indoor_ambient_temperature` | °C | `0x00` | 2 (NTC, Beta model) |
| `indoor_coil_temperature` | °C | `0x00` | 3 (NTC, Beta model) |
| `outdoor_ambient_temperature` | °C | `0x00` | 5 (NTC, Beta model) |
| `outdoor_coil_temperature` | °C | `0x00` | 4 (NTC, Beta model) |
| `discharge_temperature` | °C | `0x00` | 6 (NTC, Steinhart-Hart) |
| `operating_mode` | raw code | `0x02` | 8 |
| `compressor_frequency_target` | Hz | `0x02` | 2 |
| `compressor_frequency_actual` | Hz | `0x02` | 3 |
| `outdoor_fan_speed` | raw | `0x00` | 7+8 (uint16) |
| `eev_steps` | raw | `0x01` | 5+6 (uint16) |
| `indoor_setpoint` | °C | `0x01` | 7 (tentative mapping) |
| `input_voltage` | V | `0x01` | 3 |
| `current_draw` | A | `0x01` | 2 |
