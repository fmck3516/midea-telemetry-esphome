#!/bin/bash
#
# Stream all ODU responses from a midea-telemetry dongle to a logfile, one
# timestamped response frame per line:
#
#   1785089257 0x55006C6F8287270000A0
#   1785089257 0x5501009BB3DC00170069
#   1785089257 0x550200000000000001A8
#   ...
#
# Usage: ./logger.sh <hostname>
#   hostname   dongle host or IP, e.g. midea-telemetry-garage
#              frames are appended to <hostname>.txt
#
# Reads the dongle's SSE /events stream and keeps only the raw response
# text_sensor states. Needs `moreutils` for the `ts` timestamper.

set -euo pipefail

HOST="${1:-}"

if [ -z "$HOST" ]; then
    echo "Usage: $0 <hostname>" >&2
    exit 1
fi

OUT="${HOST}.txt"

curl -sN "http://${HOST}.local/events" \
    -H 'Accept: text/event-stream' --insecure \
    | grep --line-buffered text_sensor \
    | grep --line-buffered -v icon \
    | stdbuf -oL cut -d '"' -f12 \
    | ts '%s' \
    | tee -a "$OUT"
