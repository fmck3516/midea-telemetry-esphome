# Raw-byte-driven dashboard (experimental)

> Status: proof-of-concept for [#36](https://github.com/fmck3516/midea-telemetry-esphome/issues/36).
> The shipped dashboard is still driven by the firmware-decoded `midea`
> measurement; everything here is opt-in and lives in a separate `midea_raw`
> measurement.

The idea: instead of only storing firmware-decoded sensors, store the **raw
frame bytes** and derive everything downstream — known sensors *and*
unit-specific unknown bytes — from that one source. Mappings become Flux you
can change without reflashing, and history can be recomputed when a formula
improves.

## 1. Endpoint

`/json` exposes every response frame as a per-byte decimal array (alongside the
existing `hex`):

```json
"odu_response_bytes": {
  "0x00": [85, 0, 109, 95, 105, 114, 52, 0, 0, 208],
  "0x01": [...],
  ...
}
```

Because Telegraf fetches the whole `/json` in one request, **every byte shares a
single timestamp** — which removes the hardest part of a raw-driven design:
aligning bytes that live in different frames (e.g. the compressor-frequency
float pairs `0x02[3]` with `0x05[2]`). They already land on the same row.

## 2. Ingest

`telegraf.conf` is generated. List your dongles in the `DEVICES` array at the
top of [`gen-telegraf-conf.sh`](gen-telegraf-conf.sh) — one
`"<url> <device-tag>"` per line — and regenerate:

```bash
./gen-telegraf-conf.sh > telegraf.conf
docker compose restart telegraf
```

Every dongle gets both a decoded `midea` input and a raw `midea_raw` input from
that one list, so the two can't drift apart. Pass `--no-raw` to emit only the
decoded inputs.

Each byte lands as its own field in measurement `midea_raw`, tagged by
`device`:

| field | meaning |
|---|---|
| `0x00_2` | frame 0x00, byte 2 |
| `0x05_2` | frame 0x05, byte 2 |
| … | … |

The script emits bytes 2–8 of all 7 frames (49 fields). Bytes 0/1/9 are framing
(header/type/checksum), not telemetry.

> **Why explicit per-byte entries?** Two simpler configs silently fail:
> the classic `json` parser drops arrays of plain numbers and writes *nothing*;
> json_v2's `object` parser collapses each frame's array into one field per
> frame, keeping only the last element (the checksum). Both were observed
> against a live stack. Naming each byte is deterministic.

Verify the field names landed as expected:

```bash
influx query 'import "influxdata/influxdb/schema"
schema.measurementFieldKeys(bucket: "midea", measurement: "midea_raw")'
```

## 3. The pivot pattern

One `pivot` turns each timestamp into a wide row exposing every byte as a
column, so known and unknown are handled uniformly:

```flux
from(bucket: "midea")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "midea_raw" and r.device == "${device}")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
// each row now has r["0x00_2"], r["0x05_2"], … for every byte
```

- **Unknown byte** → plot the column directly.
- **Known sensor** → a `map()` expression over the columns.

## 4. Proof of concept: reproduce a known sensor in Flux

`indoor_ambient_temperature` is byte `0x00[2]` through the NTC β-model (see
`ntc_temp()` in the firmware). The same decode in Flux, from the raw byte:

```flux
import "math"

from(bucket: "midea")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "midea_raw" and r.device == "${device}")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> filter(fn: (r) => exists r["0x00_2"])
  |> map(fn: (r) => {
        // float(): the bytes are ingested as ints, and Flux is strictly typed -
        // mixing them with float literals below is a "type conflict" error.
        b = float(v: r["0x00_2"])
        // NTC β-model, rounded to nearest 0.5 °C; 0x00/0xFF are fault codes.
        // (t is computed eagerly but overridden by the guards for 0/255.)
        t = 1.0 / (1.0 / 298.15 + math.log(x: 0.81 * (255.0 - b) / b) / 4150.0) - 273.15
        return {
          _time: r._time,
          _field: "indoor_ambient_temperature",
          _value:
            if b == 0.0 then -66.0
            else if b == 255.0 then 255.0
            else math.round(x: t * 2.0) / 2.0,
        }
     })
  // |> map(fn: (r) => ({ r with _value: r._value * 1.8 + 32.0 }))  // to °F
```

Overlay that on the firmware-decoded `indoor_ambient_temperature` and the two
lines should coincide — that is how you confirm a Flux decode matches the C++.
It is not a shipped panel (the dashboard's raw section is the byte explorer
below); add it ad hoc when porting a decode.

## 5. Byte explorer

The shipped [dashboard](grafana/dashboards/midea-telemetry.json) turns the raw
measurement into a browsable grid using two template variables:

| Variable | Values |
|---|---|
| `Message` (`frame`) | `0x00` … `0x06` |
| `Byte` (`byte`) | `2` … `8` |

A **repeating row** per message (`Message ${frame}`) contains a **repeating
panel** per byte, so selecting everything gives one section per response frame,
each with its seven byte charts laid out 4 + 3 (`maxPerRow: 4`). Each chart is
titled `${frame}[${byte}]` — e.g. **`0x00[2]`** — and queries
`r._field == "${frame}_${byte}"`.

Narrow either dropdown to focus: one message, or the same byte across all
messages.

These panels read `midea_raw`, so they stay empty unless the raw ingest is
enabled (see section 2). Nothing else on the dashboard depends on them.

> The ingest and field naming are confirmed against a live stack. The Flux
> decode in section 4 has not yet been checked against real data — the
> validation panel exists precisely to show whether it matches the firmware.

## Caveats

- **Reimplementing decodes in Flux** (Steinhart–Hart, the NTC fault-code
  handling, `uint16` byte-combining, the current-draw gate) must be validated
  against the tested C++ — real reimplementation and a drift risk.
- **No shared-function story** in Grafana + Flux: query-time formulas get
  duplicated across panels. Once a formula stabilizes, materialize it with an
  InfluxDB `task` that writes a decoded measurement, and keep the panels simple.
- **Keep the firmware decode.** Home Assistant and the standalone web dashboard
  still consume decoded `sensors`; this only changes the InfluxDB/Grafana lane.
- **Storage:** all bytes every scrape is ~7× the write volume; the `dedup`
  processor collapses the mostly-static frames.
