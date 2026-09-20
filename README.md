# midea-telemetry-esphome

An [ESPHome](https://esphome.io/) component to feed test-port telemetry from Midea mini-splits and centrally ducted units into Home Assistant. No Home Assistant? It works standalone too: the dongle serves its own web dashboard and JSON API, with an optional InfluxDB + Grafana stack for long-term history.

It supports a variety of brands including MRCOOL, Cooper&Hunter, Pioneer, and Senville.

![Home Assistant Dashboard](images/ha-dashboard.png)

## Compatibility

The dongle has been tested successfully with the following outdoor units:

| Brand | Outdoor Models | Type | 
|---|---|---|
| MRCOOL | DIY-12-HP-C-115C25 | single-zone mini-split |
| Cooper&Hunter | CH-HPR06F9-230VO, CH-N36LCU-230VO | single-zone mini-split |
| Senville | SENDC-36-HF-OG | centrally ducted unit |
| ACiQ | ES-48Z-M6C | multi-zone mini-split |

Since Midea-made mini-splits and centrally ducted units are very similar across brands, many other units from brands like MRCOOL, Cooper&Hunter, Senville, Pioneer, Blueridge, etc. are supported as well. To confirm, look for a test port (a 4-pin JST connector labeled `TEST`) on your unit's wiring diagram or on the outdoor unit's control or auxiliary board.

**Multi-zone units:** the dongle runs on multi-zone systems, but four sensors have come back empty on the ACiQ multi-zone unit:

- `indoor_ambient_temperature` (T1)
- `indoor_coil_temperature` (T2)
- `indoor_setpoint` (TT)
- `outdoor_fan_speed` (Pr)

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


I've added a jumper that ties the test port's +5V to the XIAO's 5V pin. Use the jumper to run the board straight off the ODU with no USB cable. Leave it off if the board is connected to USB. I also recommend the use of a USB isolator since ground on the test port is not referenced to earth.

## Safety

The outdoor unit runs on mains voltage, and internal capacitors can retain a dangerous charge after being unplugged. Always

- turn off the breaker,
- pull the disconnect, and
- wait several minutes and/or verify capacitors are discharged

before performing the installation. Wear appropriate PPE. Consult a qualified electrician when in doubt.

## Installation

*(See [Safety](#safety) first if you're jumping straight to this section.)*

Remove the top panel of the ODU. You'll see the control board. Remove the screws securing the control board to the ODU, then detach the cables from the cable clamps so you can lift the board for access — there's no need to unplug the cables themselves. The test port on my units is located at the front of the board. Plug in the dongle, with the red wire facing toward you. Reattach the cables to the cable clamps and secure the board back to the ODU. There should be enough clearance to tuck the dongle into the service panel — this lets you access the dongle later without needing to remove the control board again. Reinstall the top panel.

| Control board | Test port |
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
    compressor_frequency_actual_int:
      name: Compressor frequency (actual, int)
    # ... every field from the [Supported Sensors](#supported-sensors) table below is available
```

## Flashing

Flash your ESP32 with `esphome`. On macOS:

```sh
brew install esphome
esphome run example_midea_telemetry.yaml
```

## Supported Sensors

The following sensors are currently supported. `Code` is the two-letter parameter code the Midea
service manuals use for the same value, so a reading here can be matched against the one the unit
shows on its own diagnostic display; `—` means the manuals define no code for that value.

| Code | Name | Unit |
|---|---|---|
| TT | `indoor_setpoint` | °C |
| T1 | `indoor_ambient_temperature` | °C |
| T2 | `indoor_coil_temperature` | °C |
| T3 | `outdoor_coil_temperature` | °C |
| T4 | `outdoor_ambient_temperature` | °C |
| TP | `discharge_temperature` | °C |
| — | `operating_mode` | raw |
| oT | `compressor_frequency_indoor_target` | Hz |
| FT | `compressor_frequency_outdoor_target` | Hz |
| Fr | `compressor_frequency_actual_int` | Hz |
| Fr | `compressor_frequency_actual_float` | Hz |
| — | `compressor_frequency_outdoor_control` | Hz |
| Pr | `outdoor_fan_speed` | raw |
| Lr | `eev_steps` | raw |
| dL | `current_draw` | A |
| Ac | `input_voltage` | V |
| Uo | `dc_bus_voltage` | V |
| — | `error_code_1` | raw (E3, E0, P8, Eb, P6, PA, L3, L0) |
| — | `error_code_2` | raw (E60, E61, L2, E2, E1, L1, P90, P91) |
| — | `error_code_3` | raw (E80, E81, E1, E83, P0, P1, E5, P8) |
| — | `error_code_4` | raw (P4, P6, PA, L5, L3, L2, E7) |

[FRAME-BYTES.md](FRAME-BYTES.md) documents each sensor in great detail.

### Raw frame bytes (optional)

The table above is the *decoded* view. The unit also returns bytes nobody has mapped yet, and those differ between models — so if your unit does something the decoded sensors do not explain, the raw bytes are where to look. [FRAME-BYTES.md](FRAME-BYTES.md) lists what is known or suspected about each one.

Bytes 2–8 of all seven response frames (49 in total) can each be published to Home Assistant as-is, with no interpretation. They are **off by default**; uncomment the ones you want in the `sensor:` block:

```yaml
sensor:
  - platform: midea_telemetry
    indoor_ambient_temperature:
      name: Indoor ambient temperature

    # raw_0x01_3: { name: "Raw 0x01[3]" }
    raw_0x01_4: { name: "Raw 0x01[4]" }     # enabled
    # raw_0x01_5: { name: "Raw 0x01[5]" }
```

[`example_midea_telemetry.yaml`](example_midea_telemetry.yaml) lists all 49 commented out, grouped by frame, ready to uncomment.

They appear in Home Assistant under the device's **Diagnostic** entities. Enable only the bytes you are investigating: each one publishes on every poll, and that adds up in Home Assistant's recorder database. For watching bytes over days, use the [Grafana byte explorer](influxdb-grafana/README.md#byte-explorer) instead.

## JSON endpoint

Add `expose_json_endpoint: true` to serve all mapped sensors and the underlying raw data as JSON. It needs the `web_server` component:

```yaml
web_server:
  port: 80

midea_telemetry:
  clk_pin: GPIO3
  dat_pin: GPIO2
  expose_json_endpoint: true
```

The endpoint is served at `/json` (e.g. `http://midea-telemetry.local/json`), independent of Home Assistant. It has four sections:
- `sensors`: decoded values, null when stale or never received
- `sensor_bytes`: the raw byte(s) each value derives from (keyed `0x<response type>[<byte index>]`)
- `odu_responses`: the latest full frame per response type, as hex
- `odu_response_bytes`: the same frames as per-byte decimal arrays (e.g. `"0x00": [85, 0, 109, …]`) — the indexable form for tools like Telegraf. The [InfluxDB + Grafana stack](influxdb-grafana/README.md#how-the-dashboard-decodes-bytes) stores these bytes and decodes every chart from them.

Example:

```json
{
  "sensors": {
    "indoor_ambient_temperature": 24.5,
    "outdoor_fan_speed": 300,
    "dc_bus_voltage": 372,
    ...
  },
  "sensor_bytes": {
    "indoor_ambient_temperature": { "0x00[2]": 112 },
    "outdoor_fan_speed": { "0x00[7]": 44, "0x00[8]": 1 },
    "dc_bus_voltage": { "0x03[6]": 202 },
    ...
  },
  "odu_responses": {
    "0x00": "0x550070529794621F033A",
    "0x01": "0x55013797B3F1006202D4",
    ...
  },
  "odu_response_bytes": {
    "0x00": [85, 0, 112, 82, 151, 148, 98, 31, 3, 58],
    "0x01": [85, 1, 55, 151, 179, 241, 0, 98, 2, 212],
    ...
  }
}
```

This is useful for reverse engineering, scripting, and for using the device without Home Assistant.

## Long-term history (InfluxDB + Grafana)

For a permanent, Home-Assistant-independent history, [`influxdb-grafana/`](influxdb-grafana/) provides a ready-to-run Docker stack: Telegraf polls each dongle's `/json` endpoint and stores the raw frame bytes in InfluxDB v2, and Grafana decodes them into a provisioned dashboard. Copy `.env.example` to `.env`, list your dongles in `gen-telegraf-conf.sh`, generate `telegraf.conf`, and `docker compose up -d`. See [influxdb-grafana/README.md](influxdb-grafana/README.md).

![Grafana Dashboard](images/grafana-dashboard.png)

## Prior Art

I've documented the test-port protocol in great detail on Medium: [Reverse Engineering Midea's ODU Diagnostic Port](https://medium.com/@florian.mckee/reverse-engineering-mideas-odu-diagnostic-port-af603e159053). The firmware in this repository is based on those findings. Start there if you want to understand the protocol; the byte mappings and conversion formulas in the [Supported Sensors](#supported-sensors) table come straight from it.

## Warranty

This is a hobby project. I've permanently installed the telemetry module on all of my units without any problems. That said, use it at your own risk. I do not assume any liability if it causes damage to your equipment. See [LICENSE](LICENSE) for additional information.

## Disclaimer

Midea is a trademark of Midea Group. This is an independent, unofficial hobby project and is not affiliated with, authorized, endorsed by, or sponsored by Midea. The name is used only to describe which hardware the project interoperates with. All product names and trademarks are the property of their respective owners.
