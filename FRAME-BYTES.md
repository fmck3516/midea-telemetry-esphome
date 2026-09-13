# Frame bytes

What every byte of every diagnostic-bus message means, as far as it is known, and how the decoded
ones are converted. The [Supported Sensors](README.md#supported-sensors) table in the README only
lists the sensors. Look here for which bytes a sensor comes from, or what an unmapped byte like
`0x03[4]` might be.

Bytes are written `0x<type>[<index>]`, the notation used by the README, `/json`, the Grafana
byte explorer and the [raw byte sensors](README.md#raw-frame-bytes-optional). The index is
0-based within the 10-byte frame.

## Status

Every byte carries one of these, and they are never mixed:

| Status | Meaning |
|---|---|
| **framing** | Header, type or checksum. Not telemetry. |
| **decoded** | The firmware decodes it into a sensor. The formula is under [Encodings](#encodings). |
| **hypothesis** | Someone has proposed a meaning, but it is not verified and not decoded. Treat as a lead, not a fact. |
| **unknown** | No proposed meaning. |

A byte moves from *hypothesis* to *decoded* only when the firmware starts decoding it.

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

| Byte | Meaning | Status |
|---|---|---|
| `0x02[6]` = `0xFF` | Unknown. It is the only non-zero request payload byte. The tester sends it, so the dongle does too. | unknown |
| every other payload byte | Always `0x00` | unknown |

## Responses

Bytes 0, 1 and 9 of every response are **framing**, as shown above. The tables below cover bytes 2–8.

### `0x00`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | T1 | Indoor ambient temperature | [NTC β-model](#ntc-thermistors) | decoded | `indoor_ambient_temperature` |
| 3 | T2 | Indoor coil temperature | [NTC β-model](#ntc-thermistors) | decoded | `indoor_coil_temperature` |
| 4 | T3 | Outdoor coil temperature | [NTC β-model](#ntc-thermistors) | decoded | `outdoor_coil_temperature` |
| 5 | T4 | Outdoor ambient temperature | [NTC β-model](#ntc-thermistors) | decoded | `outdoor_ambient_temperature` |
| 6 | TP | Compressor discharge temperature | [Steinhart–Hart](#discharge-thermistor) | decoded | `discharge_temperature` |
| 7 | Pr | Outdoor fan speed, low byte | uint16 LE with byte 8 | decoded | `outdoor_fan_speed` |
| 8 | Pr | Outdoor fan speed, high byte | uint16 LE with byte 7 | decoded | `outdoor_fan_speed` |

Every payload byte of `0x00` is decoded.

### `0x01`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | dL | Compressor current draw | `0.117 · b + 0.92` A, [gated on the compressor](#current-draw) | decoded | `current_draw`, [#16](https://github.com/fmck3516/midea-telemetry-esphome/issues/16) |
| 3 | Ac | AC input voltage | `⌊b · 32/25 + 40⌋` V | decoded | `input_voltage` |
| 4 | — | Once decoded as IPM temperature, but that was never confirmed and has been removed. Its behaviour follows thermal-protection thresholds more than a temperature curve. | — | hypothesis | [#45](https://github.com/fmck3516/midea-telemetry-esphome/issues/45), [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912/60) |
| 5 | Lr | EEV opening steps, low byte | uint16 LE with byte 6 | decoded | `eev_steps` |
| 6 | Lr | EEV opening steps, high byte | uint16 LE with byte 5 | decoded | `eev_steps` |
| 7 | TT | Indoor set-point | `b < 50 ? b : (b − 50) / 2` °C, [two OEM encodings](#indoor-set-point) | decoded | `indoor_setpoint` |
| 8 | — | "High output" flag when `b == 4` | boolean | hypothesis | [#33](https://github.com/fmck3516/midea-telemetry-esphome/issues/33), [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912/41) |

The same community sketch in #33 also labels bytes 5–6 `fan_drive_level`. The tester display
identifies them as EEV steps, and that is how the firmware decodes them.

### `0x02`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | FT | Compressor frequency, outdoor target | `b` Hz | decoded | `compressor_frequency_outdoor_target`, [#34](https://github.com/fmck3516/midea-telemetry-esphome/issues/34) |
| 3 | Fr | Compressor frequency, actual (integer part) | `b` Hz. Also gates `current_draw`. | decoded | `compressor_frequency_actual_int`, `compressor_frequency_actual_float` |
| 4 | — | Protection / frequency-limit bit field. Bit 7 was seen set during an evaporator-temperature protection event, which matches the manuals' L0 limit. The other bits are unmapped. | bit field | hypothesis | [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912) |
| 5 | — | — | — | unknown | |
| 6 | — | — | — | unknown | |
| 7 | — | — | — | unknown | |
| 8 | — | Operating mode | [Mode code](#operating-mode) | decoded | `operating_mode` |

### `0x03`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | — | — | — | unknown | |
| 3 | — | — | — | unknown | |
| 4 | — | EEV zone "row bound", paired with byte 5 | — | hypothesis | [#33](https://github.com/fmck3516/midea-telemetry-esphome/issues/33), [#40](https://github.com/fmck3516/midea-telemetry-esphome/issues/40) |
| 5 | — | EEV zone "row index", paired with byte 4 | — | hypothesis | [#33](https://github.com/fmck3516/midea-telemetry-esphome/issues/33), [#40](https://github.com/fmck3516/midea-telemetry-esphome/issues/40) |
| 6 | Uo | DC bus voltage | `round(b · 59/32 − 1)` V | decoded | `dc_bus_voltage` |
| 7 | — | — | — | unknown | |
| 8 | — | — | — | unknown | |

The row names for bytes 4 and 5 are labels, not explanations. Nobody in the
[forum thread](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912/60)
could say what they mean. Both bytes also update more slowly than the sensor bytes. #40 tests
whether `0x03[4] − 3.5` tracks `0x03[5] + 3.5`.

### `0x04`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | — | — | — | unknown | |
| 3 | — | — | — | unknown | |
| 4 | — | — | — | unknown | |
| 5 | — | — | — | unknown | |
| 6 | — | Frequency-limit symbol bit field. A value of 16 (bit 4) was seen during a low-T2 frequency limit, which matches the manuals' frequency-limit symbol table. The other bits are unmapped. | bit field | hypothesis | [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912) |
| 7 | — | Compressor frequency, outdoor control | `b` Hz | decoded | `compressor_frequency_outdoor_control`, [#34](https://github.com/fmck3516/midea-telemetry-esphome/issues/34) |
| 8 | oT | Compressor frequency, indoor target (the IDU's request to the ODU) | `b` Hz | decoded | `compressor_frequency_indoor_target`, [#34](https://github.com/fmck3516/midea-telemetry-esphome/issues/34) |

### `0x05`

| Byte | Code | Meaning | Encoding | Status | Source |
|---|---|---|---|---|---|
| 2 | Fr | Compressor frequency, actual (hundredths) | `0x02[3] + b / 100` Hz | decoded | `compressor_frequency_actual_float`, [#34](https://github.com/fmck3516/midea-telemetry-esphome/issues/34) |
| 3 | — | EEV zone command from the IDU. 0 means the IDU is not in control. | — | hypothesis | [#33](https://github.com/fmck3516/midea-telemetry-esphome/issues/33), [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912/41) |
| 4 | — | — | — | unknown | |
| 5 | — | Moves on some units, with no proposed meaning | — | unknown | [forum](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits/1015912) |
| 6 | — | — | — | unknown | |
| 7 | — | — | — | unknown | |
| 8 | — | — | — | unknown | |

Some `0x05` responses arrive with an all-zero payload. The log viewer's *Hide all-zero 0x05
responses* option filters them out.

### `0x06`

| Byte | Meaning | Status |
|---|---|---|
| 2–8 | Always `0x00` on every unit reported so far | unknown |

## Encodings

The formulas behind the *decoded* bytes. `b` is the raw byte value.

### NTC thermistors

`0x00[2]`–`0x00[5]` (T1–T4). NTC β-model, rounded to the nearest 0.5 °C:

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

### Indoor set-point

`0x01[7]` (TT). `b < 50 ? b : (b − 50) / 2` °C.

Two OEM encodings, told apart by range (a real set-point is ~16–32 °C): whole-degree (16–32) or
half-degree +50 (82–114).

### Current draw

`0x01[2]` (dL). `0.117 · b + 0.92` A, truncated to 0.01 A.

The byte only carries a meaningful current while the compressor runs. When it is stopped (unit
OFF or FAN ONLY) the byte sits at a per-unit floor (3 on the 115V MRCOOL, 0 on the 220V Cooper &
Hunter) that the formula would misread as ~1 A. So `current_draw` reports the ~0.2 A standby
baseline measured with a clamp meter whenever `compressor_frequency_actual_int` (`0x02[3]`) is 0
([#16](https://github.com/fmck3516/midea-telemetry-esphome/issues/16)).

### Operating mode

`0x02[8]`. `operating_mode` is a raw integer code:

| Code | Mode | Code | Mode |
|---|---|---|---|
| 0 | OFF | 4 | DRY |
| 1 | COOL | 5 | RESERVED |
| 2 | HEAT | 6 | FORCE COOL |
| 3 | ONLY FAN | 7 | DEFROST |

Map it to text in Home Assistant with a template sensor. The bundled
[Grafana dashboard](influxdb-grafana/) already renders it as a labeled card plus a mode-history
timeline.

### Service-manual codes without a sensor

Two documented codes have no sensor here: `Ir` (indoor fan speed) and `Hu` (humidity). Neither
appears in the frames this component decodes, and `Hu` also requires a humidity sensor most units
lack. The manuals call `Lr` *EXV* opening steps; `eev_steps` is the same value under the more
common spelling.

## Keeping this file current

This file is the per-byte reference, so it has to change whenever a byte's meaning changes:

- **Adding, changing or removing a decode** in `MAPPED_PARAMS`
  ([midea_telemetry.cpp](components/midea_telemetry/midea_telemetry.cpp)): update the byte's row
  in the same PR. Set it to *decoded* with the sensor name, or back to *hypothesis* or *unknown*.
  If the formula changed, update its section under [Encodings](#encodings) too, and the Flux
  decode in the [Grafana dashboard](influxdb-grafana/README.md#how-a-panel-decodes).
- **A new hypothesis** from an issue, the forum or a log capture: add it as *hypothesis* with a
  link to the evidence. Leave *unknown* rows alone until someone proposes a meaning.
- **A hypothesis ruled out:** say so in the row and keep the link, as `0x01[4]` does, so the same
  guess does not come back.
