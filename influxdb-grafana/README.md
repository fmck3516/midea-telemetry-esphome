# InfluxDB + Grafana stack

A self-contained, permanent history for your midea-telemetry dongles. Telegraf
polls each dongle's `/json` endpoint and writes the **raw frame bytes** into
InfluxDB v2. Grafana decodes them into a provisioned dashboard. No Home
Assistant required.

```
 dongle /json  ──▶  Telegraf  ──▶  InfluxDB v2  ──▶  Grafana
 (every 10 s)     (raw bytes)    (bucket: midea)    (decodes in Flux)
```

## Prerequisites

- Docker with **Compose v2**, the `docker compose` plugin. Check with
  `docker compose version`. The old Python `docker-compose` 1.x is
  unmaintained and crashes with `KeyError: 'ContainerConfig'` whenever it
  recreates a container on current Docker Engine versions, such as when
  upgrading an image. On Ubuntu, install `docker-compose-v2`, or
  `docker-compose-plugin` from Docker's own apt repository.
- Each dongle built with `expose_json_endpoint: true` (pulls in `web_server`),
  reachable on your network. See the [main README](../README.md#json-endpoint).

## Setup

1. **Configure secrets.** Copy the env template and edit it — set a real Influx
   token and passwords:

   ```bash
   cp .env.example .env
   # generate a token:  openssl rand -hex 32
   ```

2. **List your dongles, then generate `telegraf.conf`.** Edit the `DEVICES`
   array at the top of [`gen-telegraf-conf.sh`](gen-telegraf-conf.sh) — one
   `"<url> <device-tag>"` per line — and run:

   ```bash
   ./gen-telegraf-conf.sh > telegraf.conf
   ```

   > ⚠️ **Required before step 3.** `telegraf.conf` is generated and
   > git-ignored (it holds your dongles' addresses), so a fresh clone does not
   > have one. Docker bind-mounts it, and if the file is missing it silently
   > creates a *directory* of that name — after which telegraf fails to start.
   > If that happens: `rm -rf telegraf.conf`, generate it, and bring the stack
   > back up.

   > ⚠️ **mDNS caveat.** Inside a bridged Docker container, `*.local` names do
   > **not** resolve. Use each dongle's **IP address** in `DEVICES`
   > (e.g. `http://192.168.1.42/json`), or — on a Linux host only — uncomment
   > `network_mode: host` for the telegraf service in `docker-compose.yml` to
   > share the host's mDNS resolver. Assign the dongles static DHCP leases so
   > the IPs stay put.

3. **Start it.**

   ```bash
   docker compose up -d
   ```

4. **Open Grafana** at http://localhost:3000, or `http://<host>:3000` from
   another machine on your network (log in with the Grafana
   credentials from `.env`). The **Midea Telemetry** dashboard is already there
   under the *Midea Telemetry* folder, with a **Device** dropdown at the top.

> **Upgrading from an older `telegraf.conf`?** Earlier versions also wrote a
> firmware-decoded `midea` measurement, and could skip the raw bytes with
> `--no-raw`. The dashboard now reads only the raw bytes, so regenerate the
> config and restart telegraf:
> `./gen-telegraf-conf.sh > telegraf.conf && docker compose restart telegraf`.
> Data already in `midea` stays in InfluxDB, but the dashboard no longer charts
> it.

> **Upgrading from Grafana 11?** On its first start, Grafana 12.4 migrates its
> database in the `grafana-data` volume, and it can't be downgraded again. The
> dashboard and datasource are provisioned from this repo, so the volume only
> holds logins, preferences and UI edits. Back it up first if you want to keep
> those:
>
> ```bash
> docker compose stop grafana
> docker run --rm -v influxdb-grafana_grafana-data:/data -v "$PWD":/backup busybox \
>   tar czf /backup/grafana-data-11.tgz -C /data .
> docker compose pull grafana && docker compose up -d grafana
> ```
>
> The volume is named `<project>_grafana-data`, where the project defaults to
> this directory's name. Check `docker volume ls` if yours differs.

> **Upgrading from InfluxDB 2.7?** 2.9 reads your existing data in place, and
> the image skips its first-run setup when a database already exists.
> On its first start it also hashes the stored API tokens, and the plaintext
> tokens can't be read back from InfluxDB afterwards. Hashed tokens work
> exactly as before, and Telegraf, Grafana and the export tool read theirs from
> `.env`, so keep that file. Downgrading below 2.8 afterwards erases every
> token. Back up both volumes first, with InfluxDB stopped so the files are
> consistent:
>
> ```bash
> docker compose stop influxdb
> docker run --rm -v influxdb-grafana_influxdb-data:/data \
>   -v influxdb-grafana_influxdb-config:/config -v "$PWD":/backup busybox \
>   tar czf /backup/influxdb-2.7-backup.tgz -C / data config
> docker compose pull influxdb && docker compose up -d influxdb
> ```
>
> Always keep an explicit version tag on the image. `influxdb:latest` is moving
> to InfluxDB 3, which can't read v2 data and would start with an empty
> database.

## What's provisioned

| Piece | Where |
|---|---|
| InfluxDB datasource (Flux) | `grafana/provisioning/datasources/influxdb.yml` |
| Dashboard provider | `grafana/provisioning/dashboards/dashboards.yml` |
| Dashboard | `grafana/dashboards/midea-telemetry.json` |

The dashboard charts every sensor from the [Supported Sensors table](../README.md#supported-sensors):
coil/ambient temps, discharge temp, the compressor-frequency family
(indoor/outdoor target, actual as int and float, and outdoor control —
under the "Compressor Frequency (extended)" row), outdoor fan speed & EEV
steps, input/DC-bus voltage, current draw, and set-point/operating mode. Below
those sit the [error codes](#error-codes) and the
[byte explorer](#byte-explorer). Everything is filtered by the selected
device.

The dashboard refreshes every 5 minutes. Telegraf still polls every 10 s, so
pick a shorter interval from Grafana's refresh dropdown when you want to watch
changes live.

## Network access

| Service | Reachable from | Port |
|---|---|---|
| Grafana | any machine on your network | `3000` |
| InfluxDB | this host only (`127.0.0.1`) | `8086` |
| Telegraf | nowhere, nothing is published | — |

Telegraf and Grafana reach InfluxDB over the compose network at
`http://influxdb:8086`, so InfluxDB doesn't need to be on your network. Its UI
and `tools/export-dashboard-data.py` still work on the host itself, where
`localhost:8086` is available. Run the export tool there.

Don't rely on a host firewall such as `ufw` to close a published port. Docker
adds its own firewall rules for published ports, and they bypass `ufw`. The
binding in `docker-compose.yml` decides who can connect.

To reach InfluxDB from another machine anyway, change its port to
`"8086:8086"` and run `docker compose up -d influxdb`. That exposes InfluxDB's
API and UI to your whole network.

Grafana answers anything that can reach the host on port 3000. Don't forward
that port on your router.

## How the dashboard decodes bytes

### What is stored

Telegraf writes one measurement, `midea_raw`, tagged by `device`. Each field is
one byte of one response frame, named `<frame>_<byte>`:

| Field | Meaning |
|---|---|
| `0x00_2` | response `0x00`, byte 2 |
| `0x05_2` | response `0x05`, byte 2 |
| … | bytes 2–8 of all seven responses, 49 fields |

Bytes 0, 1 and 9 are framing (header, response type, checksum) and are not
stored. What each byte means is documented in [FRAME-BYTES.md](../FRAME-BYTES.md).

Every byte comes from the same `/json` request, so all 49 share one timestamp.
That is what lets a value combine bytes from different frames, like the
compressor frequency float (`0x02[3]` + `0x05[2]`) or the current-draw gate on
`0x02[3]`.

The `dedup` processor drops a scrape in which no byte changed, and passes one
through at least every 5 minutes. Charts of a steady value therefore have fewer
points, not gaps.

> **Why one config entry per byte?** Two simpler Telegraf configs fail
> silently: the classic `json` parser drops arrays of plain numbers and writes
> nothing, and json_v2's `object` parser collapses each frame's array into one
> field that keeps only the last element (the checksum). Naming each byte is
> deterministic.

### How a panel decodes

Each panel's query:

1. filters to the bytes it needs;
2. has InfluxDB keep only the **last sample per chart window** (the stat cards
   keep only the latest sample);
3. pivots those samples into one row per timestamp;
4. applies the formula from the byte's block in
   [FRAME-BYTES.md](../FRAME-BYTES.md);
5. names the result after the firmware sensor, so legends and exported CSV
   columns keep the sensor names.

Step 2 has to come before the pivot. Up to that point InfluxDB's storage
engine runs the query itself. From the pivot on, every row is processed one at
a time, and doing that for every raw point kept `influxd` at about four CPU
cores while the dashboard still refreshed every 10 s.

It keeps the *last* sample rather than the average because all 49 bytes of a
scrape are written together. The last sample of each byte in a window therefore
comes from the same scrape, so the two halves of a 16-bit value, and a reading
and its current-draw gate, always belong together. Averaging each byte on its
own would mix scrapes.

For example, `indoor_ambient_temperature`:

```flux
import "math"

from(bucket: "${bucket}")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "midea_raw" and r.device == "${device}")
  |> filter(fn: (r) => r._field == "0x00_2")
  |> aggregateWindow(every: v.windowPeriod, fn: last, createEmpty: false)
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> filter(fn: (r) => exists r["0x00_2"])
  |> map(fn: (r) => {
        b = float(v: r["0x00_2"])
        // NTC β-model, rounded to the nearest 0.5 °C; 0 and 255 are fault codes
        ntc = (b) => math.round(x: (1.0 / (1.0 / 298.15 + math.log(x: 0.81 * (255.0 - b) / b) / 4150.0) - 273.15) * 2.0) / 2.0

        return {_time: r._time, _field: "indoor_ambient_temperature", _value: if b == 0.0 then -66.0 else if b == 255.0 then 255.0 else ntc(b: b)}
     })
  |> group(columns: ["_field"])
  |> sort(columns: ["_time"])
  |> map(fn: (r) => ({ r with _value: r._value * 1.8 + 32.0 }))
```

Because the formula lives in the query, fixing a decode needs no reflash. The
whole history is recomputed the next time the panel loads.

**Keep the decodes in step with the firmware.** Home Assistant and the dongle's
own web page still use the firmware's decode, so the same formula exists in two
places. A decode change updates both, plus
[FRAME-BYTES.md](../FRAME-BYTES.md#keeping-this-file-current).

Known differences from the firmware:

- **Charts show the last reading in each window**, not the average. The two
  only differ when a window spans several scrapes, which happens when zoomed
  far out.
- **Current draw** is 0.01 A higher on six raw values (10, 30, 70, 130, 140,
  150). The firmware computes in float32, lands just under a whole hundredth
  and truncates. Flux computes in float64 and gets the exact value. Every
  other decode matches the firmware for all 256 byte values.
- **Stale frames** show as a flat line, not a gap. The firmware reports a
  decoded sensor as unavailable once its frame is 60 s old. `/json` keeps
  serving the last raw frame received.

### Error codes

The **Error Codes** row holds one stat card per error bit field of response
`0x02`, each titled with the codes it carries and showing the byte value —
green on `0`, red on anything else:

| Card | Byte | Sensor |
|---|---|---|
| Error Code E3, E0, P8, Eb, P6, PA, L3, L0 | `0x02[4]` | `error_code_1` |
| Error Code E60, E61, L2, E2, E1, L1, P90, P91 | `0x02[5]` | `error_code_2` |
| Error Code E80, E81, E1, E83, P0, P1, E5, P8 | `0x02[6]` | `error_code_3` |
| Error Code P4, P6, PA, L5, L3, L2, E7 | `0x02[7]` | `error_code_4` |

The codes are listed from bit 0 (value 1) up to bit 7 (value 128), so a card
reading `20` has bits 2 and 4 set — the third and fifth code in its title.
[FRAME-BYTES.md](../FRAME-BYTES.md#response-type-0x02) spells out every bit.

The row is **collapsed by default**: on a healthy unit all four read `0`, and
four cards of zeroes are not worth the vertical space. Open it when a chart
shows something unexplained.

### Byte explorer

The bottom of the dashboard charts undecoded bytes, for hunting new mappings.
Two variables drive it:

| Variable | Values |
|---|---|
| `Message` (`frame`) | `0x00` … `0x06` |
| `Byte` (`byte`) | `2` … `8` |

A repeating **Message** row per response frame holds one chart per byte, four
to a line, each titled like **`0x00[2]`**. Narrow either dropdown to focus on
one message, or on the same byte across all messages.

## Export the dashboard data

[`tools/export-dashboard-data.py`](../tools/export-dashboard-data.py) writes the
data behind **every chart on the dashboard** to CSV — one file per chart, per
device — for the last *N* days:

```bash
./tools/export-dashboard-data.py --days 7
```

```
exports/2025-08-30T09-14-02/
├── manifest.json                  # panel → file, columns, and the Flux run
├── bedroom/
│   ├── t1-indoor-temperature.csv  # time,indoor_ambient_temperature
│   ├── mode-set-point.csv         # multi-target panels get one column each
│   └── 0x00-2.csv                 # byte explorer charts too
└── garage/…
```

It reads the queries out of `grafana/dashboards/midea-telemetry.json` and runs
them against InfluxDB directly, so Grafana does not have to be up — only
InfluxDB. Credentials come from `.env` (override with `$INFLUX_*` or the flags).
Standard library only; no `pip install`.

| Flag | Default |
|---|---|
| `--days N` | `7` — how far back to export (fractional days are fine) |
| `--device TAG` | every device in the bucket; repeatable |
| `--interval` | `auto` (~2000 points/series); set `1m`, `5m`, … to fix it |
| `--out DIR` | `tools/exports/<timestamp>` |
| `--dry-run` | print the Flux queries instead of running them |

The hidden per-panel target that pins the Y axis to zero (the `${ymin}` helper)
is skipped — it carries no data.

## Verify data is flowing

```bash
docker compose logs -f telegraf     # should show no connection errors
```

On the host, in the InfluxDB UI (http://localhost:8086) → *Data Explorer*, query the `midea`
bucket for measurement `midea_raw`. You should see fields `0x00_2` … `0x06_8`,
tagged by `device`. Or list the field keys in the script editor:

```flux
import "influxdata/influxdb/schema"

schema.measurementFieldKeys(bucket: "midea", measurement: "midea_raw")
```

## Retention

`INFLUX_RETENTION` in `.env` controls how long data is kept (`0s` = forever).
Change it before first boot, or adjust the bucket's retention later in the
InfluxDB UI.

## Common tweaks

- **Poll interval:** `interval` in the `[agent]` block of
  `gen-telegraf-conf.sh`, then regenerate. Keep it ≥ the dongle's
  `update_interval` so you're not polling faster than new frames arrive.
- **Change the bucket name:** update `INFLUX_BUCKET` in `.env` **and** the hidden
  `bucket` constant in the dashboard (Dashboard settings → Variables → `bucket`).
- **Add/remove a dongle:** edit `DEVICES` in `gen-telegraf-conf.sh`, then
  `./gen-telegraf-conf.sh > telegraf.conf && docker compose restart telegraf`.
  New devices appear in the dropdown automatically.
- **Change a decode:** edit the panel's Flux in
  `grafana/dashboards/midea-telemetry.json`, and keep it in step with the
  firmware (see [How a panel decodes](#how-a-panel-decodes)).

## Reset

```bash
docker compose down          # stop, keep data
docker compose down -v       # stop and delete all stored data
```
