#!/usr/bin/env python3
"""Export the data behind every chart in the Grafana dashboard to CSV.

Reads the provisioned dashboard
(`influxdb-grafana/grafana/dashboards/midea-telemetry.json`), takes each
panel's Flux query, fills in the dashboard variables and Grafana's `v.*`
macros, runs it straight against InfluxDB and writes one CSV per chart:

    exports/2026-09-04T12-00-00/bedroom/indoor-temperature.csv

Grafana itself is never contacted - the dashboard JSON is the query source and
InfluxDB answers them - so this works whether or not the Grafana container is
up. Standard library only.

Examples
--------
    # last 7 days, every device found in the bucket
    ./export-dashboard-data.py --days 7

    # last 30 days, one device, coarser sampling, into a fixed directory
    ./export-dashboard-data.py --days 30 --device bedroom \
        --interval 5m --out ~/midea-export

    # show the queries that would run, without touching InfluxDB
    ./export-dashboard-data.py --days 1 --device bedroom --dry-run

Credentials come from `influxdb-grafana/.env` (the same file docker compose
reads); real environment variables and the command-line flags win over it.
"""

import argparse
import csv
import io
import itertools
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STACK_DIR = os.path.join(REPO_ROOT, "influxdb-grafana")
DASHBOARD = os.path.join(STACK_DIR, "grafana", "dashboards", "midea-telemetry.json")
ENV_FILE = os.path.join(STACK_DIR, ".env")

# Telegraf polls every 10 s, so nothing finer than that exists.
MIN_INTERVAL_SECONDS = 10
# Points per series when --interval is left on "auto".
AUTO_TARGET_POINTS = 2000

# Columns InfluxDB adds to every annotated-CSV table; anything else is a tag we
# may need to tell two series in the same panel apart.
INTERNAL_COLUMNS = {
    "", "result", "table", "_start", "_stop", "_time", "_value",
    "_field", "_measurement",
}

VAR_RE = re.compile(r"\$\{(\w+)(?::\w+)?\}|\$(\w+)\b")


# ── dashboard parsing ────────────────────────────────────────────────────────

def load_dashboard(path):
    with open(path) as f:
        return json.load(f)


def template_options(dashboard):
    """Map each dashboard variable to the list of values it can take.

    Only the variables this script can resolve on its own are returned:
    `constant` (a single fixed value) and `custom` (a comma-separated list).
    `query` variables - i.e. `device` - are resolved against InfluxDB instead,
    and `${ymin}` never reaches a real query (see `is_axis_helper`).
    """
    options = {}
    for var in dashboard.get("templating", {}).get("list", []):
        name, kind = var.get("name"), var.get("type")
        if kind == "constant":
            options[name] = [str(var.get("query", ""))]
        elif kind == "custom":
            # "a,b,c" or "Label : value" pairs; we only want the values.
            values = []
            for part in str(var.get("query", "")).split(","):
                part = part.strip()
                if not part:
                    continue
                values.append(part.split(":")[-1].strip() if ":" in part else part)
            options[name] = values
    return options


def iter_panels(dashboard):
    """Yield every non-row panel, including those nested in collapsed rows."""
    for panel in dashboard.get("panels", []):
        if panel.get("type") == "row":
            yield from panel.get("panels", []) or []
        else:
            yield panel


def is_axis_helper(query):
    """True for the dummy `array.from` target that only anchors the Y axis.

    Every timeseries panel carries a hidden second target that emits two
    zero-valued points when the `${ymin}` variable is set to "0", forcing the
    axis to start at zero. It is a rendering trick with no data behind it, so
    it is skipped rather than exported.
    """
    return "${ymin}" in query or "array.from(" in query


def panel_queries(panel):
    """Yield (refId, query) for the real data targets of one panel."""
    for target in panel.get("targets", []) or []:
        query = target.get("query")
        if not query or target.get("hide") or is_axis_helper(query):
            continue
        yield target.get("refId", "A"), query


# ── variable substitution ────────────────────────────────────────────────────

def substitute(text, values):
    """Fill in the Grafana macros and dashboard variables of one query.

    `v.timeRangeStart` and friends are bare identifiers in the Flux source, so
    they are replaced literally; `${name}` / `$name` variables go through the
    regex. Unknown names are left untouched - `expansions` relies on spotting
    the ones that survive.
    """
    for name, value in values.items():
        if name.startswith("v."):
            text = text.replace(name, value)

    def repl(match):
        name = match.group(1) or match.group(2)
        return values.get(name, match.group(0))
    return VAR_RE.sub(repl, text)


def unresolved_vars(text):
    """Names of the variables still present in `text`, in order of appearance."""
    names = []
    for match in VAR_RE.finditer(text):
        name = match.group(1) or match.group(2)
        if name not in names:
            names.append(name)
    return names


def expansions(panel, base_values, options):
    """Yield one `{var: value}` binding per repeated instance of a panel.

    A panel with `repeat: byte` sitting under a row with `repeat: frame` is
    drawn once per (frame, byte) pair. Rather than reconstructing that nesting
    from the layout, we look at which variables the panel's title and queries
    still mention after the scalar substitutions and take the cartesian product
    of their options - which gives the same set of charts.
    """
    text = panel.get("title", "") + "".join(q for _, q in panel_queries(panel))
    names = [
        n for n in unresolved_vars(substitute(text, base_values))
        if n in options and len(options[n]) > 0
    ]
    if not names:
        yield {}
        return
    for combo in itertools.product(*(options[n] for n in names)):
        yield dict(zip(names, combo))


# ── InfluxDB ─────────────────────────────────────────────────────────────────

class Influx:
    def __init__(self, url, token, org):
        self.url = url.rstrip("/")
        self.token = token
        self.org = org

    def query(self, flux, timeout=120):
        """Run a Flux query and return the raw annotated CSV response."""
        endpoint = f"{self.url}/api/v2/query?" + urllib.parse.urlencode({"org": self.org})
        request = urllib.request.Request(
            endpoint,
            data=flux.encode(),
            method="POST",
            headers={
                "Authorization": f"Token {self.token}",
                "Content-Type": "application/vnd.flux",
                "Accept": "application/csv",
            },
        )
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read().decode()

    def devices(self, bucket, days):
        """Distinct `device` tag values seen in the export window."""
        flux = (
            'import "influxdata/influxdb/schema"\n'
            f'schema.tagValues(bucket: "{bucket}", tag: "device", '
            f'predicate: (r) => true, start: -{int(days * 86400)}s)'
        )
        return sorted(
            row["_value"] for row in parse_annotated_csv(self.query(flux))
            if row.get("_value")
        )


def parse_annotated_csv(text):
    """Yield each data row of an annotated-CSV response as a dict.

    The response is a sequence of tables, each preceded by `#datatype` /
    `#group` / `#default` annotation lines and its own header row, with blank
    lines in between - so the header is re-read whenever a new block starts.
    """
    header = None
    for row in csv.reader(io.StringIO(text)):
        if not row or all(cell == "" for cell in row):
            header = None
            continue
        if row[0].startswith("#"):
            header = None
            continue
        if header is None:
            header = row
            continue
        yield dict(zip(header, row))


# ── shaping the result ───────────────────────────────────────────────────────

def column_name(row, fallback):
    """Name the column a result row belongs to: its field, plus any stray tags.

    Most panels drop the `device`/`url` tags, so `_field` alone is unique. When
    a panel does return several series per field, the remaining tag values are
    appended so the columns stay distinct instead of overwriting each other.
    """
    name = row.get("_field") or fallback
    extra = [
        f"{k}={v}" for k, v in sorted(row.items())
        if k not in INTERNAL_COLUMNS and v not in ("", None)
    ]
    return f"{name} ({', '.join(extra)})" if extra else name


def collect(rows_by_target):
    """Pivot per-target rows into a time-keyed table plus its column order."""
    table, columns = {}, []
    for ref_id, rows in rows_by_target:
        for row in rows:
            time = row.get("_time")
            if not time:
                continue
            column = column_name(row, ref_id)
            if column not in columns:
                columns.append(column)
            table.setdefault(time, {})[column] = row.get("_value", "")
    return table, columns


def write_csv(path, table, columns):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time"] + columns)
        for time in sorted(table):
            values = table[time]
            writer.writerow([time] + [values.get(c, "") for c in columns])


def slugify(title):
    slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    return slug or "panel"


# ── configuration ────────────────────────────────────────────────────────────

def read_env_file(path):
    """Parse the docker compose `.env` file into a dict (missing file = {})."""
    values = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                values[key.strip()] = value.strip().strip('"').strip("'")
    except FileNotFoundError:
        pass
    return values


def setting(cli_value, env_key, env_file, default=None):
    """Resolve one setting: CLI flag, then real env var, then .env, then default."""
    return cli_value or os.environ.get(env_key) or env_file.get(env_key) or default


def auto_interval(days):
    """Pick an aggregation window that keeps series near AUTO_TARGET_POINTS."""
    seconds = max(MIN_INTERVAL_SECONDS, int(days * 86400 / AUTO_TARGET_POINTS))
    return f"{seconds}s"


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Export the data behind every Grafana dashboard chart to CSV.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Credentials default to influxdb-grafana/.env; $INFLUX_* and the "
               "flags below override it.",
    )
    parser.add_argument("--days", type=float, default=7,
                        help="how far back to export, in days (default: 7)")
    parser.add_argument("--device", action="append", metavar="TAG",
                        help="device tag to export; repeatable "
                             "(default: every device in the bucket)")
    parser.add_argument("--out", metavar="DIR",
                        help="output directory (default: tools/exports/<timestamp>)")
    parser.add_argument("--interval", default="auto", metavar="DURATION",
                        help="aggregation window, e.g. 1m, 5m (default: auto, "
                             f"~{AUTO_TARGET_POINTS} points per series)")
    parser.add_argument("--dashboard", default=DASHBOARD, metavar="PATH",
                        help="dashboard JSON to read the queries from")
    parser.add_argument("--influx-url", metavar="URL",
                        help="InfluxDB base URL (default: http://localhost:8086)")
    parser.add_argument("--token", metavar="TOKEN", help="InfluxDB API token")
    parser.add_argument("--org", metavar="ORG", help="InfluxDB organisation")
    parser.add_argument("--bucket", metavar="BUCKET",
                        help="bucket to read (default: the dashboard's ${bucket})")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the queries that would run and exit")
    return parser.parse_args(argv)


# ── main ─────────────────────────────────────────────────────────────────────

def main(argv=None):
    args = parse_args(argv)
    env = read_env_file(ENV_FILE)

    dashboard = load_dashboard(args.dashboard)
    options = template_options(dashboard)

    bucket = args.bucket or env.get("INFLUX_BUCKET") or (
        options.get("bucket", ["midea"])[0])
    interval = auto_interval(args.days) if args.interval == "auto" else args.interval

    # Grafana's macros, resolved the way the dashboard's own time picker would.
    # Flux durations must be whole numbers, so a fractional --days is expressed
    # in seconds rather than as e.g. the invalid "-0.5d".
    macros = {
        "v.timeRangeStart": f"-{int(args.days * 86400)}s",
        "v.timeRangeStop": "now()",
        "v.windowPeriod": interval,
    }

    influx = Influx(
        setting(args.influx_url, "INFLUX_URL", env, "http://localhost:8086"),
        setting(args.token, "INFLUX_TOKEN", env),
        setting(args.org, "INFLUX_ORG", env, "midea"),
    )
    if not influx.token and not args.dry_run:
        sys.exit("No InfluxDB token. Pass --token, set $INFLUX_TOKEN, or create "
                 f"{ENV_FILE} (see .env.example).")

    devices = args.device
    if not devices:
        if args.dry_run:
            devices = ["<device>"]
        else:
            try:
                devices = influx.devices(bucket, args.days)
            except (urllib.error.URLError, OSError) as e:
                sys.exit(f"Could not reach InfluxDB at {influx.url}: {e}")
            if not devices:
                sys.exit(f"No devices found in bucket '{bucket}' over the last "
                         f"{args.days} days. Is telegraf writing?")

    out_dir = args.out or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "exports",
        datetime.now().strftime("%Y-%m-%dT%H-%M-%S"))

    print(f"Dashboard : {args.dashboard}")
    print(f"InfluxDB  : {influx.url}  (org={influx.org}, bucket={bucket})")
    print(f"Range     : last {args.days:g} days, every {interval}")
    print(f"Devices   : {', '.join(devices)}")
    print(f"Output    : {out_dir}\n")

    manifest, failures, written = [], 0, 0
    for device in devices:
        base = dict(macros, bucket=bucket, device=device)
        for panel in iter_panels(dashboard):
            for combo in expansions(panel, base, options):
                values = dict(base, **combo)
                title = substitute(panel.get("title", f"panel-{panel.get('id')}"),
                                   values)
                queries = [(ref, substitute(q, values))
                           for ref, q in panel_queries(panel)]
                if not queries:
                    continue

                if args.dry_run:
                    print(f"# {device} / {title}")
                    for ref, query in queries:
                        print(f"# target {ref}\n{query}\n")
                    continue

                rows_by_target, panel_failed = [], False
                for ref, query in queries:
                    try:
                        rows_by_target.append(
                            (ref, list(parse_annotated_csv(influx.query(query)))))
                    except urllib.error.HTTPError as e:
                        detail = e.read().decode(errors="replace").strip()
                        print(f"  ! {device} / {title} [{ref}]: HTTP {e.code} "
                              f"{detail}", file=sys.stderr)
                        panel_failed = True
                    except (urllib.error.URLError, OSError) as e:
                        # A dead endpoint will not fix itself over the next
                        # ~70 panels, so stop rather than retry it each time.
                        sys.exit(f"\nLost connection to InfluxDB at "
                                 f"{influx.url}: {e}")
                if panel_failed:
                    failures += 1
                    continue

                table, columns = collect(rows_by_target)
                path = os.path.join(out_dir, slugify(device),
                                    f"{slugify(title)}.csv")
                write_csv(path, table, columns)
                written += 1
                print(f"  {os.path.relpath(path, out_dir)}  "
                      f"({len(table)} rows, {len(columns)} series)")
                manifest.append({
                    "device": device,
                    "panel": title,
                    "panel_id": panel.get("id"),
                    "type": panel.get("type"),
                    "file": os.path.relpath(path, out_dir),
                    "rows": len(table),
                    "columns": columns,
                    "queries": {ref: query for ref, query in queries},
                })

    if args.dry_run:
        return 0

    meta = {
        "exported_at": datetime.now(timezone.utc).isoformat(),
        "dashboard": os.path.relpath(args.dashboard, REPO_ROOT),
        "range_days": args.days,
        "window": interval,
        "bucket": bucket,
        "devices": devices,
        "panels": manifest,
    }
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(meta, f, indent=2)

    print(f"\n{written} CSV file(s) in {out_dir}")
    if failures:
        print(f"{failures} panel(s) failed - see the errors above.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
