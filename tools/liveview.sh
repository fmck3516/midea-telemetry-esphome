#!/bin/bash
#
# Live view of a midea-telemetry dongle's decoded sensor values. Polls the
# dongle's /json endpoint once a second and redraws the `sensors` object in
# place at the top of the terminal:
#
#   {
#     "indoor_ambient_temperature": 24.5,
#     "outdoor_fan_speed": 300,
#     "dc_bus_voltage": 372,
#     ...
#   }
#
# Usage: ./liveview.sh <hostname>
#   hostname   dongle host or IP, e.g. midea-telemetry-garage
#              (queries http://<hostname>.local/json)
#
# The dongle must be built with `expose_json_endpoint: true` (which pulls in
# the `web_server` component). Needs `curl` and `jq`. The `-4` flag avoids the
# ~5 s macOS `.local` IPv6-lookup stall. Press Ctrl-C to quit.

set -euo pipefail

HOST="${1:-}"

if [ -z "$HOST" ]; then
    echo "Usage: $0 <hostname>" >&2
    exit 1
fi


watch -n 1 -c "curl -4 -sN "http://${HOST}.local/json" | jq '.sensors'"