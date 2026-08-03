#!/usr/bin/env python3
"""Interactive live view of a midea-telemetry dongle's /json endpoint.

Walks you through a short wizard (use the up/down arrows and Enter):

  1. pick a dongle (hard-coded list below),
  2. pick what to watch - all sensors, all raw ODU responses, or one sensor
     in detail (its value plus the raw bytes it derives from),

then polls /json once a second and redraws the result in place at the top of
the terminal. Press Ctrl-C to quit.

The dongle must be built with `expose_json_endpoint: true` (which pulls in the
`web_server` component). Standard library only - no curl or jq needed.
"""

import json
import socket
import sys
import time
import urllib.request

try:
    import termios
    import tty

    _HAS_TERMIOS = True
except ImportError:  # non-Unix: fall back to the numbered prompt
    _HAS_TERMIOS = False

# Your dongles. Edit to match your setup; each is queried at
# http://<name>.local/json.
HOSTS = [
    "midea-telemetry-garage",
    "midea-telemetry-bedroom",
    "midea-telemetry-bathroom",
]

MODES = [
    "All sensors",
    "All raw ODU responses",
    "A single sensor (value + source bytes)",
]

POLL_SECONDS = 1
TIMEOUT_SECONDS = 4


def pick(title, options):
    """Choose one option and return its index (0-based).

    Uses an arrow-key menu on a real terminal (up/down or j/k to move, Enter to
    select, q/Esc to cancel), and falls back to a numbered prompt otherwise so
    piped input and tests still work.
    """
    if _HAS_TERMIOS and sys.stdin.isatty():
        return _pick_arrows(title, options)
    return _pick_numbered(title, options)


def _pick_numbered(title, options):
    print(title)
    for i, opt in enumerate(options, 1):
        print(f"  {i}) {opt}")
    while True:
        choice = input("> ").strip()
        if choice.isdigit() and 1 <= int(choice) <= len(options):
            return int(choice) - 1
        print(f"Please enter a number between 1 and {len(options)}.")


def _read_key():
    """Read one keypress, decoding arrow-key escape sequences."""
    ch = sys.stdin.read(1)
    if ch != "\x1b":
        return ch
    # Escape: could be a bare Esc or the start of a "\x1b[A" cursor sequence.
    if sys.stdin.read(1) == "[":
        return {"A": "up", "B": "down"}.get(sys.stdin.read(1), "other")
    return "esc"


def _pick_arrows(title, options):
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    idx = 0

    def draw():
        for i, opt in enumerate(options):
            line = f"\033[7m❯ {opt}\033[0m" if i == idx else f"  {opt}"
            sys.stdout.write(f"\r\033[K{line}\n")
        sys.stdout.flush()

    try:
        tty.setcbreak(fd)
        sys.stdout.write("\033[?25l")  # hide cursor
        print(title)
        draw()
        while True:
            key = _read_key()
            if key in ("up", "k"):
                idx = (idx - 1) % len(options)
            elif key in ("down", "j"):
                idx = (idx + 1) % len(options)
            elif key in ("\r", "\n"):
                return idx
            elif key in ("q", "esc", "\x03"):  # q / Esc / Ctrl-C
                raise KeyboardInterrupt
            else:
                continue
            sys.stdout.write(f"\033[{len(options)}A")  # back to first option
            draw()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sys.stdout.write("\033[?25h")  # restore cursor
        sys.stdout.flush()


def resolve_ipv4(host):
    """Resolve <host> to an IPv4 address once, up front.

    Restricting the lookup to AF_INET (A records only) sidesteps the ~5 s
    macOS `.local` stall, where mDNS waits out the AAAA/IPv6 query timeout.
    Reusing the address also avoids re-resolving on every poll.
    """
    infos = socket.getaddrinfo(host, 80, socket.AF_INET, socket.SOCK_STREAM)
    return infos[0][4][0]


def fetch(url):
    with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as resp:
        return json.load(resp)


def render(data, mode_idx, sensor):
    """Build the body text to display for one poll."""
    if mode_idx == 0:
        return json.dumps(data.get("sensors", {}), indent=2)
    if mode_idx == 1:
        return json.dumps(data.get("odu_responses", {}), indent=2)

    # Single-sensor detail.
    value = data.get("sensors", {}).get(sensor)
    source = data.get("source_bytes", {}).get(sensor, {})
    lines = [sensor, f"  value: {json.dumps(value)}", "  source_bytes:"]
    if source:
        lines += [f"    {k}: {json.dumps(v)}" for k, v in source.items()]
    else:
        lines.append("    (none)")
    return "\n".join(lines)


def main():
    host = HOSTS[pick("Select a dongle:", HOSTS)]
    mode_idx = pick("\nWhat do you want to watch?", MODES)

    try:
        ip = resolve_ipv4(f"{host}.local")
    except OSError as e:
        sys.exit(f"Could not resolve {host}.local: {e}")
    url = f"http://{ip}/json"

    sensor = None
    if mode_idx == 2:
        try:
            names = sorted(fetch(url).get("sensors", {}))
        except OSError as e:
            sys.exit(f"Could not reach {url}: {e}")
        if not names:
            sys.exit("The endpoint returned no sensors.")
        sensor = names[pick("\nWhich sensor?", names)]

    label = sensor if mode_idx == 2 else MODES[mode_idx]

    sys.stdout.write("\033[?25l")  # hide cursor while looping
    try:
        while True:
            try:
                body = render(fetch(url), mode_idx, sensor)
            except (OSError, ValueError) as e:
                body = f"(fetch error: {e})"
            header = f"{host}.local ({ip}) · {label} · {time.strftime('%H:%M:%S')}"
            # \033[H homes the cursor, \033[J clears to the end of the screen, so
            # each frame overwrites the previous one from the top cleanly.
            sys.stdout.write(f"\033[H\033[J{header}\n\n{body}\n")
            sys.stdout.flush()
            time.sleep(POLL_SECONDS)
    except KeyboardInterrupt:
        pass
    finally:
        sys.stdout.write("\033[?25h\n")  # restore cursor
        sys.stdout.flush()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.stdout.write("\033[?25h\n")  # restore cursor if cancelled mid-wizard
        sys.exit(130)
