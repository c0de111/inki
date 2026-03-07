# inki-monitor (private, WIP)

`inki-monitor` is a lightweight companion service for **inki** telemetry and monitoring of battery voltages, wifi signal strength, and more.
It runs as a small Python process on a LAN host.

## Current scope

- `POST /api/v1/telemetry` ingest endpoint (Bearer token)
- SQLite storage (flattened fields + raw `payload_json`)
- Plotly.js browser-rendered device charts (device page, Plotly served locally)
- Device list and per-device page with selectable extra subplots

## Firmware-side assumptions

Firmware telemetry is configured in the inki web interface (`/device_settings`) and sends:

- host (IPv4 literal)
- port
- Bearer token
- timeout
- optional label

Current firmware behavior:

- HTTP only (no HTTPS yet)
- IPv4 literal host only (no DNS yet)
- API path is hardcoded in firmware: `/api/v1/telemetry`
- telemetry is sent after the main query (before Wi-Fi deinit)
- telemetry failure does not abort the main inki flow

## Quick start (native Python)

```bash
cd inki-monitor
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
cp config.example.yaml config.yaml
# edit config.yaml (especially auth.ingest_token)
python3 app.py
```

Open `http://<host>:3004/`.

Use `python -m pip ...` (instead of bare `pip`) to ensure the virtualenv's pip is used.

### Raspberry / Debian notes (venv + pip)

If a venv is created but `pip install ...` still reports an externally managed environment (PEP 668), the venv may have been created without its own `pip`.

Typical symptom:

- `which python` points to `.venv/bin/python`
- `which pip` still points to `/usr/bin/pip`

Fix (generic):

```bash
sudo apt install -y python3-venv python3-full
rm -rf .venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

On Raspberry Pi systems with 32-bit userspace, the previous matplotlib-based version could stall during dependency builds (`contourpy`). The current Plotly.js browser-rendered chart path avoids that Python plotting dependency chain.

## Config (`config.yaml`)

Important keys:

- `server.bind_host`: `0.0.0.0` for LAN testing, `127.0.0.1` behind a reverse proxy
- `server.bind_port`: default `3004`
- `auth.ingest_token`: global ingest token (`Authorization: Bearer <token>`)
- `storage.sqlite_path`: SQLite DB location
- `limits.max_payload_bytes`: ingest size guard (default `8192`)

## Telemetry payload shape (current API)

Top-level fields (current, complete): `device_id`, `label`, `query_ok`, `wifi_rssi_dbm`, `fw_version`, `use_case`, `room_type`, `rtc_backend`, `metrics`.

Numeric measurements/timings in `metrics` (current, complete):

- `telemetry_send_elapsed_ms`
- `battery_before_wifi_v`
- `battery_after_wifi_v`
- `coin_cell_v`
- `pico_temp_c`

`inki-monitor` derives and stores:

- `battery_sag_v = battery_before_wifi_v - battery_after_wifi_v`

The full original request JSON is preserved in `samples.payload_json`.

## Inspecting non-flattened telemetry data

Not every telemetry field is flattened into dedicated SQLite columns. All original request fields are still stored in `samples.payload_json`.

This is useful when:

- trying new metrics before deciding to flatten them
- inspecting fields from older/newer firmware payloads
- debugging payload content without changing the schema

## Running from repo (current practice)

Running directly from the repo checkout (`venv` + `python3 app.py`) is the intended development workflow and is also acceptable for local/private deployment. Plotly is served locally from `static/vendor/` (no CDN dependency at runtime).

## Running as a user service (`systemd --user`)

For longer-running use on a host machine, use a user-level `systemd` service with the repo checkout and virtualenv.

Template unit file:

- `systemd/inki-monitor.service.example`

Install (user service, no `/etc/systemd/system` changes):

```bash
mkdir -p ~/.config/systemd/user
cp /path/to/inki-private/inki-monitor/systemd/inki-monitor.service.example ~/.config/systemd/user/inki-monitor.service
$EDITOR ~/.config/systemd/user/inki-monitor.service  # adjust WorkingDirectory / ExecStart paths
systemctl --user daemon-reload
systemctl --user enable --now inki-monitor
```

Verify:

```bash
systemctl --user status inki-monitor
journalctl --user -u inki-monitor -n 30 --no-pager
journalctl --user -u inki-monitor -f
curl http://127.0.0.1:3004/healthz
```

Daily management:

```bash
systemctl --user stop inki-monitor
systemctl --user start inki-monitor
systemctl --user restart inki-monitor
journalctl --user -u inki-monitor -f
```

Optional (keeps the user service running after logout and across reboots):

```bash
sudo loginctl enable-linger "$USER"
```

Update workflow (manual git update):

```bash
cd /path/to/inki-private
git switch develop
git pull --ff-only origin develop
cd inki-monitor
. .venv/bin/activate
python -m pip install -r requirements.txt
systemctl --user restart inki-monitor
```

Convenience helpers:

```bash
cd /path/to/inki-private/inki-monitor
./inki-monitor-install.sh   # first-time host bootstrap (venv + service + health check)
./inki-monitor-maintain.sh  # local maintenance (backup + deps + restart + health check)
./inki-monitor-check.sh     # read-only status/health/DB/venv/dependency check
```

`./inki-monitor-install.sh` does:
- create `.venv` if missing and install Python dependencies (`python -m pip`, no manual venv activation required)
- create `config.yaml` from `config.example.yaml` if missing (keeps existing config if present)
- install/update the user `systemd` service from `systemd/inki-monitor.service.example`
- patch `WorkingDirectory` / `ExecStart` in the copied unit to the current checkout path
- `systemctl --user enable --now inki-monitor`
- local health check (`http://127.0.0.1:3004/healthz`)

`./inki-monitor-maintain.sh` does:
- print current repo branch/commit/status (manual visibility only)
- stop `systemctl --user inki-monitor` for a clean update window
- WAL-safe SQLite backup to `~/inki-monitor-YYYYMMDD-HHMMSS.sqlite3` (via `.venv` Python `sqlite3` backup API; no host `sqlite3` CLI required)
- `python -m pip install -r requirements.txt` in the existing `.venv`
- start `systemctl --user inki-monitor`
- local health check (`http://127.0.0.1:3004/healthz`)

`./inki-monitor-maintain.sh` intentionally does **not** run `git fetch`, `git pull`, or `git switch` because those affect the full repo checkout, not only `inki-monitor`.

`./inki-monitor-check.sh` is read-only and reports:
- repo branch/commit/dirty state (no fetch/pull/switch)
- `systemctl --user` enabled/active status
- local health check
- SQLite file sizes and row counts
- venv Python version and outdated packages (using the current `.venv`)

## Legacy battery log import (bash)

For importing old `esign-server` battery logs (`*_queries.txt`) into the current `samples` table, use:

```bash
./import_legacy.sh --dry-run \
  --db ./data/inki-monitor.sqlite3 \
  --file /path/to/esign-server/log/103H_queries.txt \
  --device-id inki-2CCF67D929A3 \
  --label 103H_queries.txt
```

Then run without `--dry-run` to import.

Current importer behavior:

- imports into `samples` only (does **not** rewrite `devices` summary rows)
- explicit target mapping via `--device-id` (recommended; use the real `inki-<MAC>` ID)
- explicit `--label` (can be the full legacy filename for traceability)
- writes `remote_addr='legacy-import'`
- stores provenance in `payload_json` (`legacy_file`, `legacy_label`, `legacy_line`, `legacy_ts`)

## Supported & tested hosts

- Ubuntu x86_64
- Debian/Raspberry Pi (Linux on Raspberry hardware; tested with Plotly.js browser-rendered charts)
