# midea-telemetry-esphome

An [ESPHome](https://esphome.io/) component to feed diagnostic-port telemetry from Midea mini-splits into Home Assistant. It supports a variety of brands including MRCOOL, Cooper&Hunter, and Pioneer.

<img src="images/dashboard.png" width="800">

15 sensors are currently supported. See [Fields](#fields) for the full list.

## Prior Art

I've documented the diagnostic bus protocol in great detail on Medium: [Reverse Engineering Midea's ODU Diagnostic Port](https://medium.com/@florian.mckee/reverse-engineering-mideas-odu-diagnostic-port-af603e159053). The firmware in this repository is based on those findings. Start there if you want to understand the protocol; the byte mappings and conversion formulas in the [Fields](#fields) table come straight from it.

## Hardware

All you need is a **dual-core ESP32** and a level shifter. A dual core is required because the bus bit-banging runs in a dedicated FreeRTOS task. A full request/response cycle keeps the bus busy for ~380 ms, far too long to run on the main loop.

<img src="images/schematics.png" width="400">

Recommended hardware:
- [XIAO ESP32S3](https://www.amazon.com/dp/B0BYSB66S5)
- [3.3V–5V Level Shifter](https://www.amazon.com/dp/B07F7W91LC)
- [Mini PCB Prototype boards](https://www.amazon.com/dp/B081MSKJJX)

I used the following connector kits, but you can get away with a single 4-pin male JST-XH connector:
- [XH 2.54mm Connector Kit](https://www.amazon.com/dp/B08G18PWQ6)
- [JST-XHP Connector Kit](https://www.amazon.com/dp/B07CTH46S7)

The assembled prototype:

<img src="images/prototype.png" width="400">

I've added a jumper that ties the diagnostic port's +5V to the XIAO's 5V pin. Use the jumper to run the board straight off the ODU with no USB cable. Leave it off if the board is connected to USB. I also recommend the use of a USB isolator since ground on the diagnostic port is not referenced to earth.

3D printed enclosure:

<img src="images/enclosure.png" width="400">

### PCB

I've created a PCB to replace the perfboard. You solder in the ODU connector, the level shifter module and the XIAO, and that's the whole build. The KiCad project, schematic and orderable gerbers live in [pcb/](pcb/).

| Top | Bottom |
|---|---|
| <img src="pcb/pcb-top.png" width="400"> | <img src="pcb/pcb-bottom.png" width="400"> |

2-layer, 41.5 × 22 mm, 1.6 mm, ground plane on both sides, four M2 mounting holes.

Disclaimer: The PCB has not been fabricated and verified yet.

## Configuration

See [example-config/device.yaml](example-config/device.yaml) for a complete, flashable configuration with all 15 sensors. The short version:

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

### JSON endpoint

The decoded parameters and the raw diagnostic frames (e.g. `0x55006D457671401F03B0`) are useful for reverse engineering — mapping unidentified attributes or troubleshooting existing mappings — and for scripting against the device without Home Assistant.

Set `expose_json_endpoint: true` on the component and everything is served as JSON at `http://<device>/json`, independent of which sensors you've configured. Nothing is streamed to Home Assistant. Requires the `web_server` component:

```yaml
web_server:
  port: 80

midea_telemetry:
  clk_pin: GPIO3
  dat_pin: GPIO2
  expose_json_endpoint: true
```

The response has three sections: `sensors` holds every decoded value (`null` when its frame is stale or was never received); `source_bytes` gives the raw bytes each value was derived from, keyed `0x<response>[<byte>]` — handy for correlating undecoded encodings against the decoded value; and `odu_responses` holds the latest complete frame of each response type as hex:

```json
{
  "sensors": {
    "indoor_ambient_temperature": 24.5,
    "outdoor_fan_speed": 300,
    "dc_bus_voltage": 372
  },
  "source_bytes": {
    "indoor_ambient_temperature": { "0x00[2]": 112 },
    "outdoor_fan_speed": { "0x00[7]": 44, "0x00[8]": 1 },
    "dc_bus_voltage": { "0x03[6]": 202 }
  },
  "odu_responses": {
    "0x00": "0x550070529794621F033A",
    "0x01": "0x55013797B3F1006202D4",
    "0x05": null
  }
}
```

If you specifically want the raw frames *in* Home Assistant instead, expose them via the `text_sensor` platform — one entry per response type (`response_0`–`response_6`), each tracking the latest frame of that type. Note this streams the frames to HA on every update:

```yaml
text_sensor:
  - platform: midea_telemetry
    response_0:
      name: Response 0x00
    response_2:
      name: Response 0x02
    # ... response_0–response_6 are all available
```

## Flashing

Flash your ESP32 with `esphome`. On macOS:

```sh
brew install esphome
cd example-config
esphome run device.yaml
```

## Fields

Byte mapping and conversion formulas as documented in [Reverse Engineering Midea's ODU Diagnostic Port](https://medium.com/@florian.mckee/reverse-engineering-mideas-odu-diagnostic-port-af603e159053):

| Sensor | Unit | Response | Bytes | Mapping |
|---|---|---|---|---|
| `indoor_ambient_temperature` | °C | 0 | 2 | NTC β-model ¹ |
| `indoor_coil_temperature` | °C | 0 | 3 | NTC β-model ¹ |
| `outdoor_ambient_temperature` | °C | 0 | 5 | NTC β-model ¹ |
| `outdoor_coil_temperature` | °C | 0 | 4 | NTC β-model ¹ |
| `discharge_temperature` | °C | 0 | 6 | Steinhart–Hart ² |
| `ipm_temperature` | °C | 1 | 4 | NTC β-model ¹ |
| `operating_mode` | raw | 2 | 8 | `b` |
| `compressor_frequency_target` | Hz | 2 | 2 | `b` |
| `compressor_frequency_actual` | Hz | 2 | 3 | `b` |
| `outdoor_fan_speed` | raw | 0 | 7+8 | `b₇ \| b₈ << 8` (uint16 LE) |
| `eev_steps` | raw | 1 | 5+6 | `b₅ \| b₆ << 8` (uint16 LE) |
| `indoor_setpoint` | °C | 1 | 7 | `b < 50 ? b : (b − 50) / 2` ³ |
| `input_voltage` | V | 1 | 3 | `⌊b · 32/25 + 40⌋` |
| `current_draw` | A | 1 | 2 | `0.117 · b + 0.92` |
| `dc_bus_voltage` | V | 3 | 6 | `round(b · 59/32 − 1)` |

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

`operating_mode` is a raw integer code (e.g. `0` = cooling, `3` = fan). Map it to text in Home Assistant with a template sensor.

## Warranty

This is is a hobby project. I've permanently installed the telemetry module on all of my units without any problems. That said, use it at your own risk. I do not assume any liability if it causes damage to your equipment. See [LICENSE](LICENSE) for additional information.

## Safety

The outdoor unit runs on mains voltage, and internal capacitors can retain a dangerous charge after being unplugged. Always

- turn off the breaker,
- pull the disconnect, and
- wait several minutes and/or verify capacitors are discharged

before performing the installation. Wear appropriate PPE. Consult a qualified electrician when in doubt.

## Installation

*(See [Safety](#safety) first if you're jumping straight to this section.)*

 Remove the top panel of the ODU. You'll see the control board. Remove the screws securing the control board to the ODU, then detach the cables from the cable clamps so you can lift the board for access — there's no need to unplug the cables themselves. The diagnostic port is located at the front of the board. Plug in the dongle, with the red wire facing toward you. Reattach the cables to the cable clamps and secure the board back to the ODU. There should be enough clearance to tuck the dongle into the service panel — this lets you access the dongle later without needing to remove the control board again. Reinstall the top panel.

|  |  |
| --- | --- |
| <img src="images/install-1.png" width="300"><br>Control board. | <img src="images/install-2.png" width="300"><br>Diagnostic port. |
| <img src="images/install-3.png" width="300"><br>Dongle plugged in. | <img src="images/install-4.png" width="300"><br>Dongle tucked into service panel. |

For a visual walkthrough, see [this installation video](https://www.youtube.com/watch?v=poEmSZnrnjs).

## First use

Upon first start, the dongle brings up a WiFi hotspot so you can configure your WiFi settings. Join the `midea-telemetry-esphome` network (password `midea-telemetry-esphome`) and pick your network in the popup that appears. Once connected, the dongle serves a webserver at `http://midea-telemetry.local` (useful if you don't run Home Assistant), and Home Assistant automatically detects it as a new ESPHome device.

|  |  |
| --- | --- |
| <img src="images/hotspot.png" width="380"><br>On first start the dongle brings up a WiFi hotspot for configuration. | <img src="images/wifi-settings.png" width="380"><br>Join `midea-telemetry-esphome`; a popup asks which WiFi network to connect to. |
| <img src="images/webserver.png" width="380"><br>Reach the on-board webserver at `http://midea-telemetry.local`. | <img src="images/ha-auto-discovery.png" width="380"><br>Home Assistant auto-discovers the dongle as a new ESPHome device. |

## Compatibility

The dongle has been tested successfully with the following outdoor units:

| Brand | Outdoor Model |
|---|---|
| MRCOOL | DIY-12-HP-C-115C25 |
| Cooper&Hunter | CH-HPR06F9-230VO |
| Cooper&Hunter | CH-N36LCU-230VO |
<!-- | Pioneer | YN018GMFI20RPD | -->

The following outdoor unit models have been reported to lack a diagnostic port:

| Brand | Outdoor Model |
|---|---|
| Pioneer | YN036GLFI19RPE |

I haven't had a chance to analyze the diagnostic bus on a multi-head unit yet. Supporting these units will likely require firmware enhancements beyond what's currently implemented.

## Disclaimer

Midea is a trademark of Midea Group. This is an independent, unofficial hobby project and is not affiliated with, authorized, endorsed by, or sponsored by Midea. The name is used only to describe which hardware the project interoperates with. All product names and trademarks are the property of their respective owners.
