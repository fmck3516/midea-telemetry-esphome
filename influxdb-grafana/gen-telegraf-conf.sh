#!/usr/bin/env bash
# Generate the complete telegraf.conf for every midea-telemetry dongle.
#
#   ./gen-telegraf-conf.sh > telegraf.conf && docker compose restart telegraf
#
# Edit the DEVICES list below - it is the single source of truth for which
# dongles are polled. Each one gets a `midea_raw` input: the raw frame bytes,
# one explicit json_v2 field per byte, named `<frame>_<byte>`. The Grafana
# dashboard decodes every panel from those bytes (issue #53).
set -euo pipefail

# ── EDIT ME: one entry per dongle, "<url> <device-tag>" ──────────────────────
# The device tag is what the Grafana dashboard's ${device} filter selects on.
#
# IMPORTANT: inside a bridged Docker container, ".local" (mDNS) names do NOT
# resolve. Use the dongle's IP address here, or enable host networking in
# docker-compose.yml on a Linux host.
DEVICES=(
  "http://192.168.1.157/json bedroom"
  "http://192.168.1.123/json garage"
  "http://192.168.1.128/json bathroom"
)
# ─────────────────────────────────────────────────────────────────────────────

if [ $# -gt 0 ]; then
  echo "gen-telegraf-conf.sh: takes no arguments. --no-raw was removed:" \
    "the dashboard decodes every panel from the raw bytes (issue #53)." >&2
  exit 1
fi

emit_header() {
  cat <<'EOF'
# Telegraf: poll each midea-telemetry dongle's /json endpoint and write the raw
# frame bytes to InfluxDB v2. The Grafana dashboard decodes them in Flux. Each
# dongle gets its own [[inputs.http]] block so it can carry a distinct `device`
# tag.
#
# Requires the dongle firmware to be built with `expose_json_endpoint: true`.
#
# GENERATED FILE - do not edit by hand. Change the DEVICES list in
# gen-telegraf-conf.sh and re-run:
#
#   ./gen-telegraf-conf.sh > telegraf.conf && docker compose restart telegraf

[agent]
  interval = "10s"
  round_interval = true
  flush_interval = "10s"
  omit_hostname = true

[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "${INFLUX_TOKEN}"
  organization = "${INFLUX_ORG}"
  bucket = "${INFLUX_BUCKET}"

# ---------------------------------------------------------------------------
# Raw-byte ingest, one input per dongle.
#
# The dongle serves every response frame as a per-byte array under
# `odu_response_bytes` (e.g. {"0x00": [85,0,109,...]}).
#
# Needs the json_v2 parser with one explicit `field` entry per byte. Two
# simpler approaches do NOT work:
#   - the classic `json` parser silently drops arrays of plain numbers, so it
#     writes nothing at all;
#   - json_v2's `object` parser collapses each frame's byte array into a single
#     field per frame (keeping only the last element - the checksum).
# Naming each byte explicitly is deterministic across Telegraf versions and
# yields fields `<frame>_<byte>` (e.g. `0x00_2`) in the `midea_raw` measurement.
#
# Bytes 0/1/9 are framing (header / response type / checksum), not telemetry,
# so only bytes 2-8 of each of the 7 response frames are emitted (49 fields).
# ---------------------------------------------------------------------------
EOF
}

emit_raw_block() {
  local url=$1 device=$2 frame byte f

  cat <<EOF

[[inputs.http]]
  urls = ["${url}"]
  method = "GET"
  timeout = "8s"
  name_override = "midea_raw"
  data_format = "json_v2"
  [inputs.http.tags]
    device = "${device}"
  [[inputs.http.json_v2]]
EOF

  for frame in $(seq 0 6); do
    f=$(printf '0x%02X' "$frame")
    for byte in $(seq 2 8); do
      cat <<EOF
    [[inputs.http.json_v2.field]]
      path = "odu_response_bytes.${f}.${byte}"
      rename = "${f}_${byte}"
      type = "int"
EOF
    done
  done
}

emit_dedup() {
  cat <<'EOF'

# Drop scrapes in which no byte changed since the last one, so the mostly-static
# frames don't rewrite 49 values every 10 s. A metric is only dropped when all
# of its fields repeat, and one is passed through at least every dedup_interval.
[[processors.dedup]]
  namepass = ["midea_raw"]
  dedup_interval = "300s"
EOF
}

if [ ${#DEVICES[@]} -eq 0 ]; then
  echo "gen-telegraf-conf.sh: DEVICES is empty - edit the list at the top." >&2
  exit 1
fi

for entry in "${DEVICES[@]}"; do
  # shellcheck disable=SC2086 # deliberate word split into url + device
  set -- $entry
  if [ $# -ne 2 ]; then
    echo "gen-telegraf-conf.sh: bad DEVICES entry: '$entry' (want '<url> <device>')" >&2
    exit 1
  fi
done

emit_header
for entry in "${DEVICES[@]}"; do
  # shellcheck disable=SC2086
  set -- $entry
  emit_raw_block "$1" "$2"
done
emit_dedup
