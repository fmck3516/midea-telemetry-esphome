# Frame bytes

What every byte of every test-port message means, as far as it is known, and how the decoded
ones are converted. The [Supported Sensors](README.md#supported-sensors) table in the README only
lists the sensors. Look here for which bytes a sensor comes from, or what an unmapped byte like
[`0x03[4]`](#payload-0x034) might be.

Bytes are written `0x<type>[<index>]`, the notation used by the README, `/json`, the Grafana
byte explorer and the [raw byte sensors](README.md#raw-frame-bytes-optional). The index is
0-based within the 10-byte frame.

## Frame layout

Every message, in both directions, is 10 bytes, sent LSB-first:

| Byte | Request (dongle → ODU) | Response (ODU → dongle) |
|---|---|---|
| 0 | `0xAA`, the header for a message from the tester | `0x55`, the header for a message from the ODU |
| 1 | Request type, `0x00`–`0x03` | Response type, `0x00`–`0x06` |
| 2–8 | Payload | Payload |
| 9 | Checksum | Checksum |

**Checksum:** all ten bytes sum to zero modulo 256, so `checksum = (256 − sum(bytes 0–8) % 256) % 256`.
The firmware drops a response that fails this check. A response of ten `0xFF` bytes means the ODU
did not answer.

**Multi-byte values** are little-endian: the lower index is the low byte.

**Requests and responses are not paired.** The ODU steps through the seven response types on its
own, so a response is identified by its type byte, not by the request that triggered it.

## Requests

The dongle replays the four requests of Midea's handheld inverter tester, byte for byte. Their
payloads are constant, and nothing is known about what they mean.

| Type | Frame | Payload |
|---|---|---|
| `0x00` | `AA 00 00 00 00 00 00 00 00 56` | all `0x00` |
| `0x01` | `AA 01 00 00 00 00 00 00 00 55` | all `0x00` |
| `0x02` | `AA 02 00 00 00 00 FF 00 00 55` | all `0x00` except byte 6 = `0xFF` |
| `0x03` | `AA 03 00 00 00 00 00 00 00 53` | all `0x00` |

`0x02[6]` = `0xFF` is the only non-zero payload byte.

## Responses

Bytes 0, 1 and 9 of every response are **framing**, as shown above. The blocks below cover bytes 2–8.

### Response Type `0x00`

Every payload byte of `0x00` is decoded.

#### Payload `0x00[2]`

|                |   |
|----------------|---|
| Meaning        | Indoor ambient temperature |
| HA Entity      | `indoor_ambient_temperature` |
| Midea Code     | T1 |
| Encoding       | [NTC β-model](#ntc-thermistors) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[3]`

|                |   |
|----------------|---|
| Meaning        | Indoor coil temperature |
| HA Entity      | `indoor_coil_temperature` |
| Midea Code     | T2 |
| Encoding       | [NTC β-model](#ntc-thermistors) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[4]`

|                |   |
|----------------|---|
| Meaning        | Outdoor coil temperature |
| HA Entity      | `outdoor_coil_temperature` |
| Midea Code     | T3 |
| Encoding       | [NTC β-model](#ntc-thermistors) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[5]`

|                |   |
|----------------|---|
| Meaning        | Outdoor ambient temperature |
| HA Entity      | `outdoor_ambient_temperature` |
| Midea Code     | T4 |
| Encoding       | [NTC β-model](#ntc-thermistors) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[6]`

|                |   |
|----------------|---|
| Meaning        | Compressor discharge temperature |
| HA Entity      | `discharge_temperature` |
| Midea Code     | TP |
| Encoding       | [Steinhart–Hart](#discharge-thermistor) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[7]`

|                |   |
|----------------|---|
| Meaning        | Outdoor fan speed, low byte |
| HA Entity      | `outdoor_fan_speed` |
| Midea Code     | Pr |
| Encoding       | uint16 LE with [`0x00[8]`](#payload-0x008) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x00[8]`

|                |   |
|----------------|---|
| Meaning        | Outdoor fan speed, high byte |
| HA Entity      | `outdoor_fan_speed` |
| Midea Code     | Pr |
| Encoding       | uint16 LE with [`0x00[7]`](#payload-0x007) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

### Response Type `0x01`

#### Payload `0x01[2]`

|                |   |
|----------------|---|
| Meaning        | Compressor current draw |
| HA Entity      | `current_draw` |
| Midea Code     | dL |
| Encoding       | `0.117 · b + 0.92` A, truncated to 0.01 A, gated on the compressor frequency |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester. The byte only carries a meaningful current while the compressor runs. When it is stopped (unit OFF or FAN ONLY) the byte sits at a per-unit floor (3 on the 115V MRCOOL, 0 on the 220V Cooper & Hunter) that the formula would misread as ~1 A. So `current_draw` reports a ~0.2 A standby baseline. This is the value I measured with a clamp meter whenever `compressor_frequency_actual_int` (`0x02[3]`) is 0 on my mini-splits. |

#### Payload `0x01[3]`

|                |   |
|----------------|---|
| Meaning        | AC input voltage |
| HA Entity      | `input_voltage` |
| Midea Code     | Ac |
| Encoding       | `⌊b · 32/25 + 40⌋` V |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester. |

#### Payload `0x01[4]`

|                |   |
|----------------|---|
| Meaning        | IPM temperature |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | [NTC β-model](#ntc-thermistors) |
| Confidence     | Medium |
| Evidence       | The IPM-temperature reading was never confirmed and is no longer exposed as HA Entity. It only seems to carry the IPM temperature on some units: [forum](https://community.home-assistant.io/t/1015912/60) |

#### Payload `0x01[5]`

|                |   |
|----------------|---|
| Meaning        | EEV opening steps, low byte |
| HA Entity      | `eev_steps` |
| Midea Code     | Lr |
| Encoding       | uint16 LE with [`0x01[6]`](#payload-0x016) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x01[6]`

|                |   |
|----------------|---|
| Meaning        | EEV opening steps, high byte |
| HA Entity      | `eev_steps` |
| Midea Code     | Lr |
| Encoding       | uint16 LE with [`0x01[5]`](#payload-0x015) |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x01[7]`

|                |   |
|----------------|---|
| Meaning        | Indoor set-point |
| HA Entity      | `indoor_setpoint` |
| Midea Code     | TT |
| Encoding       | `b < 50 ? b : (b − 50) / 2` °C<br><br>Two OEM encodings, told apart by range (a real set-point is ~16–32 °C): whole-degree (16–32) or half-degree +50 (82–114). |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester + observations on two of my units |

#### Payload `0x01[8]`

|                |   |
|----------------|---|
| Meaning        | "High output" flag when `b == 4` |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | Bit field |
| Confidence     | Low |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/41) |

### Response Type `0x02`

#### Payload `0x02[2]`

|                |   |
|----------------|---|
| Meaning        | Compressor frequency, outdoor target |
| HA Entity      | `compressor_frequency_outdoor_target` |
| Midea Code     | FT |
| Encoding       | `b` Hz |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x02[3]`

|                |   |
|----------------|---|
| Meaning        | Compressor frequency, actual (integer part) |
| HA Entity      | `compressor_frequency_actual_int`, `compressor_frequency_actual_float` |
| Midea Code     | Fr |
| Encoding       | `b` Hz |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x02[4]`

|                |   |
|----------------|---|
| Meaning        | Error codes<br>Bit 7 = L0 (frequency limit due to low/high evaporator temperature) |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | Bit field |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester |

#### Payload `0x02[5]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x02[6]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x02[7]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x02[8]`

|                |   |
|----------------|---|
| Meaning        | Operating mode |
| HA Entity      | `operating_mode` |
| Midea Code     | — |
| Encoding       | 0 = OFF<br>1 = COOL<br>2 = HEAT<br>3 = ONLY FAN<br>4 = DRY<br>5 = RESERVED<br>6 = FORCE COOL<br>7 = DEFROST |
| Confidence     | High |
| Evidence       | Decoding established using Midea's Inverter Tester Manual |

### Response Type `0x03`

#### Payload `0x03[2]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x03[3]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x03[4]`

|                |   |
|----------------|---|
| Meaning        | EEV zone "row bound" |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | Low |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/41) |

#### Payload `0x03[5]`

|                |   |
|----------------|---|
| Meaning        | EEV zone "row index" |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | Low |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/41) |

#### Payload `0x03[6]`

|                |   |
|----------------|---|
| Meaning        | DC bus voltage |
| HA Entity      | `dc_bus_voltage` |
| Midea Code     | Uo |
| Encoding       | `round(b · 59/32 − 1)` V |
| Confidence     | Medium |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/17) |

#### Payload `0x03[7]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x03[8]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

### Response Type `0x04`

#### Payload `0x04[2]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x04[3]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x04[4]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x04[5]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x04[6]`

|                |   |
|----------------|---|
| Meaning        | Frequency-limit symbol bit field<br><br>Bit 4 = Frequency limit caused by low temperature of T2 |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | Bit field |
| Confidence     | Medium |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/71) |

#### Payload `0x04[7]`

|                |   |
|----------------|---|
| Meaning        | Compressor frequency, outdoor control |
| HA Entity      | `compressor_frequency_outdoor_control` |
| Midea Code     | — |
| Encoding       | `b` Hz |
| Confidence     | Medium |
| Evidence       | None, this mapping is a guess |

#### Payload `0x04[8]`

|                |   |
|----------------|---|
| Meaning        | Compressor frequency, indoor target (the IDU's request to the ODU) |
| HA Entity      | `compressor_frequency_indoor_target` |
| Midea Code     | oT |
| Encoding       | `b` Hz |
| Confidence     | Medium |
| Evidence       | None, this mapping is a guess |

### Response Type `0x05`

#### Payload `0x05[2]`

|                |   |
|----------------|---|
| Meaning        | Compressor frequency, actual (hundredths) |
| HA Entity      | `compressor_frequency_actual_float` |
| Midea Code     | Fr |
| Encoding       | [`0x02[3]`](#payload-0x023) `+ b / 100` Hz |
| Confidence     | Medium |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/78) |

#### Payload `0x05[3]`

|                |   |
|----------------|---|
| Meaning        | EEV zone command from the IDU. 0 means the IDU is not in control |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | Low |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912/41) |

#### Payload `0x05[4]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x05[5]`

|                |   |
|----------------|---|
| Meaning        | Moves on some units, with no proposed meaning |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |
| Evidence       | [forum](https://community.home-assistant.io/t/1015912) |

#### Payload `0x05[6]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x05[7]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

#### Payload `0x05[8]`

|                |   |
|----------------|---|
| Meaning        | — |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

### Response Type `0x06`

#### Payload `0x06[2]`–`0x06[8]`

|                |   |
|----------------|---|
| Meaning        | Always `0x00` on every unit reported so far |
| HA Entity      | — |
| Midea Code     | — |
| Encoding       | — |
| Confidence     | — |

The seven payload bytes share one block because no unit has ever reported any of them as non-zero.
Split them out if one starts to move.

## Encodings

The thermistor curves the temperature bytes use. `b` is the raw byte value.

### NTC thermistors

NTC β-model, rounded to the nearest 0.5 °C:

```
T = 1 / (1/298.15 + ln(0.81 · (255 − b) / b) / 4150) − 273.15
```

`0x00` and `0xFF` are fault codes. They read as −66 and 255.

### Discharge thermistor

`0x00[6]` (TP). Steinhart–Hart, rounded to the nearest 1 °C, with

```
L = ln((255 − b) / b)
T = 1 / (2.873×10⁻³ + 2.491×10⁻⁴ · L + 9.74×10⁻⁷ · L³) − 273.15
```

`0x00` is a fault code that reads as −48. `0xFE` and `0xFF` are fault codes passed through as-is.

## Keeping this file current

This file is the per-byte reference, so it has to change whenever a byte's meaning changes:

- **Adding, changing or removing a decode** in `MAPPED_PARAMS`
  ([midea_telemetry.cpp](components/midea_telemetry/midea_telemetry.cpp)): update the byte's block
  in the same PR. Name the sensor in **HA Entity** and give the conversion in **Encoding**, or set
  both back to `—` when a decode goes away. If the formula changed, update the Flux decode in the
  [Grafana dashboard](influxdb-grafana/README.md#how-a-panel-decodes) too.
- **A new proposal** from an issue, the forum or a log capture: put it in **Meaning**, set
  **Confidence** to `Low` or `Medium` and cite it in **Evidence**. Leave blocks with **Confidence**
  at `—` alone until someone proposes a meaning.
- **A proposal ruled out:** say so in **Evidence** and keep the link, as
  [`0x01[4]`](#payload-0x014) does, so the same guess does not come back.
