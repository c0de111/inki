# inki-monitor

Lightweight companion service for **inki** device telemetry — battery voltages, Wi-Fi signal strength, temperature, and more. Runs as a small Python/Flask process on a LAN host with SQLite storage and browser-rendered Plotly.js charts.

## Quick start

```bash
cd inki-monitor
./inki-monitor-install.sh
```

This creates a venv, installs deps, sets up a `systemd --user` service, and runs a health check. Edit `config.yaml` afterwards (especially `auth.ingest_token`).

Open `http://<host>:3004/`.

### Manual setup (if you prefer)

```bash
cd inki-monitor
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
cp config.example.yaml config.yaml
python3 app.py
```

## Helper scripts

| Script | Purpose |
|---|---|
| `inki-monitor-install.sh` | First-time bootstrap: venv, deps, config, systemd service, health check |
| `inki-monitor-maintain.sh` | Maintenance: stop, SQLite backup, reinstall deps, restart, health check |
| `inki-monitor-check.sh` | Read-only status: service state, health, DB stats, outdated packages |

All scripts are no-arg. `maintain` and `check` do **not** run git commands — repo updates are manual.

## Config (`config.yaml`)

Key settings:

- `server.bind_host` / `server.bind_port` (default `0.0.0.0:3004`)
- `auth.ingest_token`: Bearer token for telemetry ingest
- `storage.sqlite_path`: SQLite DB path

## Firmware side

Configure telemetry in the inki web interface under Device Settings: host (IPv4), port, Bearer token, timeout, optional label. Telemetry is sent after the main query, before Wi-Fi deinit. Failures do not affect the main inki flow.

Current constraints: HTTP only, IPv4 literal only, hardcoded path `/api/v1/telemetry`.

## Telemetry payload

Top-level: `device_id`, `label`, `query_ok`, `wifi_rssi_dbm`, `fw_version`, `fw_build_date`, `use_case`, `room_type`, `rtc_backend`, `metrics`.

Metrics: `telemetry_send_elapsed_ms`, `battery_before_wifi_v`, `battery_after_wifi_v`, `coin_cell_v`, `pico_temp_c`.

Derived server-side: `battery_sag_v = battery_before_wifi_v - battery_after_wifi_v`.

Full original JSON preserved in `samples.payload_json` for ad-hoc queries.

## systemd --user service

```bash
systemctl --user status inki-monitor
journalctl --user -u inki-monitor -f
curl http://127.0.0.1:3004/healthz
```

Optional (survive logout/reboot): `sudo loginctl enable-linger "$USER"`

## Legacy battery log import

```bash
./import_legacy.sh --dry-run --db ./data/inki-monitor.sqlite3 --file /path/to/log.txt --device-id inki-2CCF67D929A3 --label 103H_queries.txt
```

Remove `--dry-run` to import. Maps old eSign battery logs into `samples` with `remote_addr='legacy-import'` and provenance in `payload_json`.

## Raspberry / Debian notes

If `pip install` reports externally managed environment (PEP 668) after venv creation:

```bash
sudo apt install -y python3-venv python3-full
rm -rf .venv && python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip && python -m pip install -r requirements.txt
```

## Tested hosts

- Ubuntu x86_64
- Debian / Raspberry Pi (aarch64 and armhf)
