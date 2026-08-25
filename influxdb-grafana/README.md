# InfluxDB + Grafana stack

A self-contained, permanent history for your midea-telemetry dongles. Telegraf
polls each dongle's `/json` endpoint, writes the decoded values into InfluxDB v2,
and Grafana renders a provisioned dashboard on top — no Home Assistant required.

```
 dongle /json  ──▶  Telegraf  ──▶  InfluxDB v2  ──▶  Grafana
 (every 10 s)                     (bucket: midea)     (auto dashboard)
```

## Prerequisites

- Docker + Docker Compose
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

   Each dongle gets a decoded `midea` input plus a raw `midea_raw` input
   ([#36](https://github.com/fmck3516/midea-telemetry-esphome/issues/36)) from
   that one list; pass `--no-raw` to skip the raw ones.

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

4. **Open Grafana** at http://localhost:3000 (log in with the Grafana
   credentials from `.env`). The **Midea Telemetry** dashboard is already there
   under the *Midea Telemetry* folder, with a **Device** dropdown at the top.

## What's provisioned

| Piece | Where |
|---|---|
| InfluxDB datasource (Flux) | `grafana/provisioning/datasources/influxdb.yml` |
| Dashboard provider | `grafana/provisioning/dashboards/dashboards.yml` |
| Dashboard | `grafana/dashboards/midea-telemetry.json` |

The dashboard groups every field from the [Fields table](../README.md#fields):
coil/ambient temps, discharge & IPM temps, the compressor-frequency family
(indoor/outdoor target, actual as int and float, and outdoor control —
under the "Compressor Frequency (extended)" row at the bottom), outdoor fan
speed & EEV steps, input/DC-bus voltage, current draw, and set-point/operating
mode — filtered by the selected device(s).

It also carries an experimental raw-byte explorer at the bottom: a repeating
**Message** row per response frame, each holding a chart per byte (`0x00[2]`,
`0x00[3]`, …), driven by the `Message` and `Byte` variables — see
[RAW-BYTES.md](RAW-BYTES.md) ([#36](https://github.com/fmck3516/midea-telemetry-esphome/issues/36)).
Those panels stay empty if you generate the config with `--no-raw`; nothing
else on the dashboard depends on them.

## Verify data is flowing

```bash
docker compose logs -f telegraf     # should show no connection errors
```

In the InfluxDB UI (http://localhost:8086) → *Data Explorer*, query the `midea`
bucket for measurement `midea`; you should see one series per field, tagged by
`device`.

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

## Reset

```bash
docker compose down          # stop, keep data
docker compose down -v       # stop and delete all stored data
```
