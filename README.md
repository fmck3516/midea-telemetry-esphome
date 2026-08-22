# midea-telemetry-esphome

An [ESPHome](https://esphome.io/) component to feed diagnostic-port telemetry from Midea mini-splits and centrally ducted units into Home Assistant. No Home Assistant? It works standalone too: the dongle serves its own web dashboard and JSON API, with an optional InfluxDB + Grafana stack for long-term history.

It supports a variety of brands including MRCOOL, Cooper&Hunter, Pioneer, and Senville.

![Home Assistant Dashboard](images/ha-dashboard.png)

## Supported Sensors

The following sensors are currently supported:

| Sensor | Unit | Bytes | Mapping |
|---|---|---|---|
| `indoor_ambient_temperature` | °C | 0x00[2] | NTC β-model ¹ |
| `indoor_coil_temperature` | °C | 0x00[3] | NTC β-model ¹ |
| `outdoor_ambient_temperature` | °C | 0x00[5] | NTC β-model ¹ |
| `outdoor_coil_temperature` | °C | 0x00[4] | NTC β-model ¹ |
| `discharge_temperature` | °C | 0x00[6] | Steinhart–Hart ² |
| `ipm_temperature` | °C | 0x01[4] | NTC β-model ¹ |
| `operating_mode` | raw | 0x02[8] | `b` |
| `compressor_frequency_target` | Hz | 0x02[2] | `b` |
| `compressor_frequency_actual` | Hz | 0x02[3] | `b` |
| `outdoor_fan_speed` | raw | 0x00[7+8] | `b₇ \| b₈ << 8` (uint16 LE) |
| `eev_steps` | raw | 0x01[5+6] | `b₅ \| b₆ << 8` (uint16 LE) |
| `indoor_setpoint` | °C | 0x01[7] | `b < 50 ? b : (b − 50) / 2` ³ |
| `input_voltage` | V | 0x01[3] | `⌊b · 32/25 + 40⌋` |
| `current_draw` | A | 0x01[2] | `0.117 · b + 0.92` ⁴ |
| `dc_bus_voltage` | V | 0x03[6] | `round(b · 59/32 − 1)` |
| `compressor_frequency_fine` | Hz | 0x02[3] + 0x05[2] | `b₀₂₋₃ + b₀₅₋₂ / 100` ⁵ |
| `compressor_frequency_odu` | Hz | 0x04[7] | `b` ⁵ |
| `indoor_demand_frequency` | Hz | 0x04[8] | `b` ⁵ |
| `high_output_flag` | raw | 0x01[8] | `b == 4 ? 1 : 0` ⁵ |
| `fan_drive_level` | raw | 0x01[5+6] | `b₅ \| b₆ << 8` (uint16 LE) ⁵ |
| `eev_zone_command` | raw | 0x05[3] | `b` ⁵ |
| `eev_zone_row_index` | raw | 0x03[5] | `b` ⁵ |
| `eev_zone_row_bound` | raw | 0x03[4] | `b` ⁵ |

Where `b` is the raw byte value.

¹ NTC β-model, rounded to the nearest 0.5 °C:
```
T = 1 / (1/298.15 + ln(0.81 · (255 − b) / b) / 4150) − 273.15
```

² Steinhart–Hart, with
```
L = ln((255 − b) / b)
T = 1 / (2.873×10⁻³ + 2.491×10⁻⁴ · L + 9.74×10⁻⁷ · L³) − 273.15
```

³ Two OEM encodings, told apart by range (a real set-point is ~16–32 °C): whole-degree (16–32) or half-degree +50 (82–114).

⁴ The byte only carries a meaningful current while the compressor runs. When it is stopped (unit OFF or FAN ONLY) the byte sits at a per-unit floor (3 on the 115V MRCOOL, 0 on the 220V Cooper & Hunter) that the formula would misread as ~1 A, so `current_draw` reports the ~0.2 A standby baseline measured with a clamp meter whenever `compressor_frequency_actual` (response 2, byte 3) is 0.

⁵ Community-contributed mappings ([#33](https://github.com/fmck3516/midea-telemetry-esphome/issues/33)), still tentative — confirmed on a single unit and not yet cross-checked across OEMs. `fan_drive_level` decodes the same bytes as `eev_steps`; the two are alternative interpretations of that 16-bit field. Enable the ones you want and cross-reference against the `/json` endpoint before relying on them.

`operating_mode` is a raw integer code:

| Code | Mode | Code | Mode |
|---|---|---|---|
| 0 | OFF | 4 | DRY |
| 1 | COOL | 5 | RESERVED |
| 2 | HEAT | 6 | FORCE COOL |
| 3 | ONLY FAN | 7 | DEFROST |

Map it to text in Home Assistant with a template sensor. The bundled [Grafana dashboard](influxdb-grafana/) already renders it as a labeled card plus a mode-history timeline.

## Prior Art

I've documented the diagnostic bus protocol in great detail on Medium: [Reverse Engineering Midea's ODU Diagnostic Port](https://medium.com/@florian.mckee/reverse-engineering-mideas-odu-diagnostic-port-af603e159053). The firmware in this repository is based on those findings. Start there if you want to understand the protocol; the byte mappings and conversion formulas in the [Supported Sensors](#supported-sensors) table come straight from it.

## Hardware

All you need is a **dual-core ESP32** and a level shifter. A dual core is required because the bus bit-banging runs in a dedicated FreeRTOS task. A full request/response cycle keeps the bus busy for ~380 ms, far too long to run on the main loop.

<img src="images/schematics.png" width="400">

Recommended hardware:
- [XIAO ESP32S3](https://www.amazon.com/dp/B0BYSB66S5)
- [3.3V–5V Level Shifter](https://www.amazon.com/dp/B07F7W91LC)

I used the following connector kits, but you can get away with a single 4-pin male JST-XH connector:
- [XH 2.54mm Connector Kit](https://www.amazon.com/dp/B08G18PWQ6)
- [2.54mm Connector 4 Pin Male Adapter Right Angle](https://www.amazon.com/dp/B0BMDQLR4Q)

### PCB & Enclosure

The KiCad project, schematic, and Gerber files live in [pcb/](pcb/) - a 2-layer board, 41.5 × 22 mm, 1.6 mm thick, with four M2 mounting holes. You solder in just three parts - the ODU connector, the level-shifter module, and the XIAO - and that's the whole build. FreeCAD sources plus STL and 3MF exports for the 3D-printable enclosure are in [enclosure/](enclosure/).

| PCB (top)  | PCB (bottom)  |
| --- | --- |
| ![Top](pcb/pcb-top.png) | ![Bottom](pcb/pcb-bottom.png) |

| Assembled dongle | Enclosure |
| --- | --- |
| ![Prototype](images/prototype.jpg) | ![Enclosure](images/enclosure.png) |


I've added a jumper that ties the diagnostic port's +5V to the XIAO's 5V pin. Use the jumper to run the board straight off the ODU with no USB cable. Leave it off if the board is connected to USB. I also recommend the use of a USB isolator since ground on the diagnostic port is not referenced to earth.


## Safety

The outdoor unit runs on mains voltage, and internal capacitors can retain a dangerous charge after being unplugged. Always

- turn off the breaker,
- pull the disconnect, and
- wait several minutes and/or verify capacitors are discharged

before performing the installation. Wear appropriate PPE. Consult a qualified electrician when in doubt.

## Installation

*(See [Safety](#safety) first if you're jumping straight to this section.)*

Remove the top panel of the ODU. You'll see the control board. Remove the screws securing the control board to the ODU, then detach the cables from the cable clamps so you can lift the board for access — there's no need to unplug the cables themselves. The diagnostic port on my units is located at the front of the board. Plug in the dongle, with the red wire facing toward you. Reattach the cables to the cable clamps and secure the board back to the ODU. There should be enough clearance to tuck the dongle into the service panel — this lets you access the dongle later without needing to remove the control board again. Reinstall the top panel.

| Control board | Diagnostic port |
| --- | --- |
| ![Install 1/4](images/install-1.png) | ![Install 2/4](images/install-2.png) |


| Dongle plugged in | Dongle tucked into service panel |
| --- | --- |
| ![Install 3/4](images/install-3.png) | ![Install 4/4](images/install-4.png) |

For a visual walkthrough, see [this installation video](https://www.youtube.com/watch?v=poEmSZnrnjs).

## First use

Upon first start, the dongle brings up a temporary WiFi hotspot so you can connect it to your WiFi network. Join the `midea-telemetry-esphome` network (password `midea-telemetry-esphome`) and provide your WiFi network's SSID and password in the popup that appears. Once connected, the dongle serves a webserver at `http://midea-telemetry.local` (useful if you don't run Home Assistant), and Home Assistant automatically detects it as a new ESPHome device.

| WiFi hotspot | WiFi setup |
| --- | --- |
| ![Hotspot](images/hotspot.png) | ![Wi-Fi settings](images/wifi-settings.png) |

| On-board webserver | Home Assistant auto-discovery |
| --- | --- |
| ![Webserver](images/webserver.png) | ![Device auto discovery](images/ha-auto-discovery.png) |

## Configuration

See [example_midea_telemetry.yaml](example_midea_telemetry.yaml) for a complete configuration with every supported sensor. That file uses a local `components:` source, so it's flashable straight from a checkout of this repo; to pull the component remotely instead, switch the source to `github://fmck3516/midea-telemetry-esphome` as shown below.

The short version:

```yaml
external_components:
  - source: github://fmck3516/midea-telemetry-esphome
    components: [midea_telemetry]

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
    # ... every field from the Fields table below is available
```

## Flashing

Flash your ESP32 with `esphome`. On macOS:

```sh
brew install esphome
esphome run example_midea_telemetry.yaml
```

### JSON endpoint

Add `expose_json_endpoint: true` to serve all mapped sensors and the underlying raw data as JSON. It needs the `web_server` component:

```yaml
web_server:
  port: 80

midea_telemetry:
  clk_pin: GPIO3
  dat_pin: GPIO2
  expose_json_endpoint: true
```

The endpoint is served at `/json` (e.g. `http://midea-telemetry.local/json`), independent of Home Assistant. It has three sections:
- `sensors`: decoded values, null when stale or never received
- `source_bytes`: the raw byte(s) each value derives from (keyed `0x<response type>[<byte index>]`)
- `odu_responses`: the latest full frame per response type, as hex

Example:

```json
{
  "sensors": {
    "indoor_ambient_temperature": 24.5,
    "outdoor_fan_speed": 300,
    "dc_bus_voltage": 372,
    ...
  },
  "source_bytes": {
    "indoor_ambient_temperature": { "0x00[2]": 112 },
    "outdoor_fan_speed": { "0x00[7]": 44, "0x00[8]": 1 },
    "dc_bus_voltage": { "0x03[6]": 202 },
    ...
  },
  "odu_responses": {
    "0x00": "0x550070529794621F033A",
    "0x01": "0x55013797B3F1006202D4",
    ...
  }
}
```

This is useful for reverse engineering, scripting, and for using the device without Home Assistant.

### Long-term history (InfluxDB + Grafana)

For a permanent, Home-Assistant-independent history, [`influxdb-grafana/`](influxdb-grafana/) provides a ready-to-run Docker stack: Telegraf polls each dongle's `/json` endpoint, stores the decoded values in InfluxDB v2, and Grafana serves a provisioned dashboard on top. Copy `.env.example` to `.env`, list your dongles in `telegraf.conf`, and `docker compose up -d`. See [influxdb-grafana/README.md](influxdb-grafana/README.md).

![Grafana Dashboard](images/grafana-dashboard.png)

## Compatibility

The dongle has been tested successfully with the following outdoor units:

| Brand | Outdoor Models |
|---|---|
| MRCOOL | DIY-12-HP-C-115C25 |
| Cooper&Hunter | CH-HPR06F9-230VO, CH-N36LCU-230VO |

Since Midea-made mini-splits are very similar across brands, many other units from brands like MRCOOL, Cooper&Hunter, Senville, Pioneer, Blueridge, etc. are supported as well. Check your unit's wiring diagram, or look for a diagnostic port (white 4-pin JST connector labeled `TEST`) on the control board itself, to confirm.

**Note**: I haven't had a chance to analyze the diagnostic bus on a multi-head unit yet. Supporting these units will likely require firmware enhancements beyond what's currently implemented.

The wiring diagrams for the following outdoor units include the diagnostic port:

| Brand | Outdoor Models | Wiring Diagram |
|---|---| ---|
| MRCOOL | DIY-18-HP-C-230C, DIY-24-HP-C-230C, DIY-36-HP-C-230C  | [mc-diy-4-ah-sz-sm-en-01.pdf](https://doxrepo.mrcool.com/mc-diy-4-ah-sz-sm-en-01.pdf) |
| Pioneer | YN009GMFI22RPE, YN012GMFI22RPE, YN009GMFI20RPD, YN012GMFI20RPD, YN018GMFI20RPD, YN009AMFI22RPE, YN012AMFI22RPE, YN009AMFI20RPD, YN012AMFI20RPD, YN018GMFI22RPE, YN024GMFI22RPE, YN024GMFI20RPD, YN030GMFI20RPD, YN036GMFI20RPD  | [WYS_SM.pdf](https://www.pdhvac.com/site/downloads/WYS_SM.pdf) |

The following outdoor unit models reportedly lack a diagnostic port, or their wiring diagrams show no such port:

| Brand | Outdoor Model | Wiring Diagram |
|---|---|---|
| Pioneer | YN036GLFI19RPE | n/a |
| Carrier | 38MARBQ24AA3 | [SG-38MARB-02.pdf](https://www.shareddocs.com/hvac/docs/1009/Public/03/SG-38MARB-02.pdf) |

## Warranty

This is a hobby project. I've permanently installed the telemetry module on all of my units without any problems. That said, use it at your own risk. I do not assume any liability if it causes damage to your equipment. See [LICENSE](LICENSE) for additional information.

## Disclaimer

Midea is a trademark of Midea Group. This is an independent, unofficial hobby project and is not affiliated with, authorized, endorsed by, or sponsored by Midea. The name is used only to describe which hardware the project interoperates with. All product names and trademarks are the property of their respective owners.
