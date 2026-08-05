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

2. **List your dongles.** Edit [`telegraf.conf`](telegraf.conf): one
   `[[inputs.http]]` block per dongle, each with its URL and a unique `device`
   tag. The file ships with `bedroom`, `garage`, and `bathroom` as examples.

   > ⚠️ **mDNS caveat.** Inside a bridged Docker container, `*.local` names do
   > **not** resolve. Use each dongle's **IP address** in `telegraf.conf`
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
coil/ambient temps, discharge & IPM temps, compressor frequency (target vs
actual), outdoor fan speed & EEV steps, input/DC-bus voltage, current draw, and
set-point/operating mode — filtered by the selected device(s).

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

- **Poll interval:** `interval` in `telegraf.conf` (`[agent]`). Keep it ≥ the
  dongle's `update_interval` so you're not polling faster than new frames arrive.
- **Change the bucket name:** update `INFLUX_BUCKET` in `.env` **and** the hidden
  `bucket` constant in the dashboard (Dashboard settings → Variables → `bucket`).
- **Add/remove a dongle:** add or delete an `[[inputs.http]]` block and
  `docker compose restart telegraf`. New devices appear in the dropdown
  automatically.

## Reset

```bash
docker compose down          # stop, keep data
docker compose down -v       # stop and delete all stored data
```
