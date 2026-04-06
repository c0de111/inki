from __future__ import annotations

import json
import os
import re
import subprocess
import time
from pathlib import Path
from typing import Any

import yaml
from flask import Flask, Response, abort, jsonify, render_template, request

import db

DEVICE_ID_RE = re.compile(r"^inki-[0-9A-F]{12}$")


def _load_yaml(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle) or {}
    if not isinstance(data, dict):
        raise ValueError(f"Config root must be a mapping: {path}")
    return data


def load_config() -> tuple[dict[str, Any], Path]:
    app_dir = Path(__file__).resolve().parent
    env_path = os.environ.get("INKI_MONITOR_CONFIG")
    if env_path:
        cfg_path = Path(env_path).expanduser().resolve()
    else:
        cfg_path = app_dir / "config.yaml"

    if cfg_path.exists():
        config = _load_yaml(cfg_path)
        return config, cfg_path

    fallback = app_dir / "config.example.yaml"
    if fallback.exists():
        config = _load_yaml(fallback)
        return config, fallback

    raise FileNotFoundError("No config file found (expected INKI_MONITOR_CONFIG or inki-monitor/config.yaml)")


def _cfg_get(config: dict[str, Any], path: list[str], default: Any = None) -> Any:
    cur: Any = config
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def _get_bearer_token() -> str | None:
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        return None
    return auth[7:].strip()


def _get_remote_addr() -> str | None:
    forwarded_for = request.headers.get("X-Forwarded-For", "")
    if forwarded_for:
        return forwarded_for.split(",", 1)[0].strip() or None
    return request.remote_addr


def _query_flag(name: str) -> bool:
    value = request.args.get(name)
    if value is None:
        return False
    return value not in ("", "0", "false", "False", "off", "no")


def _safe_int(value: object, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _apply_legacy_window(rows: list[Any], l_param: object, s_param: object) -> tuple[list[Any], int, int]:
    n = len(rows)
    if n == 0:
        return [], 0, 0

    length = max(0, _safe_int(l_param, 0))
    start_value = max(0, _safe_int(s_param, 0))

    if length == 0 and start_value == 0:
        start = 0
        end = n
    elif length > 0 and start_value == 0:
        end = n
        start = max(0, n - length)
    elif length > 0 and start_value > 0:
        start = min(n, start_value - 1)
        end = min(n, start + length)
    else:
        start = min(n, start_value - 1)
        end = n

    return rows[start:end], start, end


def _as_optional_bool(value: Any) -> bool | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)) and value in (0, 1):
        return bool(value)
    raise ValueError("must be boolean")


def _as_optional_float(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    raise ValueError("must be numeric")


def _as_optional_int(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool):
        raise ValueError("must be integer")
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    raise ValueError("must be integer")


def _as_optional_str(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        return value
    raise ValueError("must be string")


def _normalize_payload(payload: dict[str, Any], received_at_unix_s: int) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ValueError("JSON body must be an object")

    device_id = payload.get("device_id")
    if not isinstance(device_id, str) or not DEVICE_ID_RE.fullmatch(device_id):
        raise ValueError("device_id missing or invalid (expected inki-<12 uppercase hex>)")

    metrics = payload.get("metrics")
    if not isinstance(metrics, dict):
        raise ValueError("metrics must be an object")

    battery_before_wifi_v = _as_optional_float(metrics.get("battery_before_wifi_v"))
    battery_after_wifi_v = _as_optional_float(metrics.get("battery_after_wifi_v"))
    coin_cell_v = _as_optional_float(metrics.get("coin_cell_v"))
    pico_temp_c = _as_optional_float(metrics.get("pico_temp_c"))
    telemetry_send_elapsed_ms = _as_optional_int(metrics.get("telemetry_send_elapsed_ms"))

    if battery_before_wifi_v is None or battery_after_wifi_v is None:
        raise ValueError("metrics.battery_before_wifi_v and metrics.battery_after_wifi_v are required")
    if coin_cell_v is None:
        raise ValueError("metrics.coin_cell_v is required")
    if pico_temp_c is None:
        raise ValueError("metrics.pico_temp_c is required")
    if telemetry_send_elapsed_ms is None:
        raise ValueError("metrics.telemetry_send_elapsed_ms is required")

    battery_sag_v = battery_before_wifi_v - battery_after_wifi_v

    return {
        "device_id": device_id,
        "label": _as_optional_str(payload.get("label")),
        "query_ok": _as_optional_bool(payload.get("query_ok")),
        "wifi_rssi_dbm": _as_optional_float(payload.get("wifi_rssi_dbm")),
        "fw_version": _as_optional_str(payload.get("fw_version")),
        "fw_build_date": _as_optional_str(payload.get("fw_build_date")),
        "rtc_backend": _as_optional_str(payload.get("rtc_backend")),
        "wake_source": _as_optional_str(payload.get("wake_source")),
        "use_case": _as_optional_str(payload.get("use_case")),
        "room_type": _as_optional_str(payload.get("room_type")),
        "telemetry_send_elapsed_ms": telemetry_send_elapsed_ms,
        "battery_before_wifi_v": battery_before_wifi_v,
        "battery_after_wifi_v": battery_after_wifi_v,
        "battery_sag_v": battery_sag_v,
        "coin_cell_v": coin_cell_v,
        "pico_temp_c": pico_temp_c,
        "received_at_unix_s": received_at_unix_s,
    }


def _plot_sample_row_to_dict(row: Any) -> dict[str, Any]:
    return {
        "received_at_unix_s": int(row["received_at_unix_s"]),
        "telemetry_send_elapsed_ms": row["telemetry_send_elapsed_ms"],
        "battery_before_wifi_v": row["battery_before_wifi_v"],
        "battery_after_wifi_v": row["battery_after_wifi_v"],
        "battery_sag_v": row["battery_sag_v"],
        "coin_cell_v": row["coin_cell_v"],
        "pico_temp_c": row["pico_temp_c"],
        "query_ok": row["query_ok"],
        "wifi_rssi_dbm": row["wifi_rssi_dbm"],
        "wake_source": row["wake_source"] if "wake_source" in row.keys() else None,
    }


def _repo_signature_info(repo_root: Path) -> dict[str, str]:
    def git_out(*args: str) -> str:
        try:
            result = subprocess.run(
                ["git", "-C", str(repo_root), *args],
                check=True,
                capture_output=True,
                text=True,
            )
            return result.stdout.strip()
        except Exception:
            return "n/a"

    return {
        "branch": git_out("rev-parse", "--abbrev-ref", "HEAD"),
        "signature": git_out("rev-parse", "--short", "HEAD"),
        "build_date": git_out("show", "-s", "--date=short", "--format=%cd", "HEAD"),
    }


def _format_last_seen_ago(ts_unix_s: Any, now_unix_s: int | None = None) -> str:
    if ts_unix_s is None:
        return "n/a"
    try:
        ts = int(ts_unix_s)
    except (TypeError, ValueError):
        return "n/a"
    now = int(time.time()) if now_unix_s is None else int(now_unix_s)
    delta = max(0, now - ts)
    if delta < 60:
        return "just now"
    if delta < 3600:
        return f"{delta // 60} min ago"
    if delta < 172800:
        return f"{delta // 3600} h ago"
    return f"{delta // 86400} d ago"


def create_app() -> Flask:
    app = Flask(__name__, template_folder="templates", static_folder="static")

    config, config_path = load_config()
    app.config["INKI_MONITOR_CONFIG"] = config
    app.config["INKI_MONITOR_CONFIG_PATH"] = str(config_path)

    app_dir = Path(__file__).resolve().parent
    repo_info = _repo_signature_info(app_dir.parent)
    app.config["INKI_MONITOR_REPO_INFO"] = repo_info
    sqlite_path = Path(_cfg_get(config, ["storage", "sqlite_path"], "./data/inki-monitor.sqlite3"))
    if not sqlite_path.is_absolute():
        sqlite_path = (app_dir / sqlite_path).resolve()

    schema_path = app_dir / "sql" / "schema.sql"
    try:
        db.init_db(sqlite_path, schema_path)
    except RuntimeError as exc:
        raise RuntimeError(
            f"inki-monitor database initialization failed for '{sqlite_path}'. {exc}"
        ) from exc
    app.config["INKI_MONITOR_DB_PATH"] = str(sqlite_path)

    @app.get("/healthz")
    def healthz() -> Response:
        return Response("ok\n", mimetype="text/plain")

    @app.post("/api/v1/telemetry")
    def ingest_telemetry() -> Response:
        cfg = app.config["INKI_MONITOR_CONFIG"]
        max_payload_bytes = int(_cfg_get(cfg, ["limits", "max_payload_bytes"], 8192))

        content_length = request.content_length
        if content_length is not None and content_length > max_payload_bytes:
            return jsonify({"ok": False, "error": "payload too large"}), 413

        token_expected = str(_cfg_get(cfg, ["auth", "ingest_token"], ""))
        token_got = _get_bearer_token()
        if not token_expected or token_got != token_expected:
            return jsonify({"ok": False, "error": "unauthorized"}), 401

        payload = request.get_json(silent=True)
        if payload is None:
            return jsonify({"ok": False, "error": "invalid JSON"}), 400

        received_at_unix_s = int(time.time())
        try:
            normalized = _normalize_payload(payload, received_at_unix_s)
        except ValueError as exc:
            return jsonify({"ok": False, "error": str(exc)}), 400

        raw_payload_json = json.dumps(payload, separators=(",", ":"), sort_keys=True)
        remote_addr = _get_remote_addr()

        try:
            db.insert_sample_and_update_device(
                app.config["INKI_MONITOR_DB_PATH"],
                normalized,
                raw_payload_json,
                remote_addr,
            )
        except Exception as exc:  # pragma: no cover - surfaced to client and logs
            app.logger.exception("Failed to store telemetry")
            return jsonify({"ok": False, "error": f"db error: {exc}"}), 500

        return jsonify(
            {
                "ok": True,
                "device_id": normalized["device_id"],
                "received_at_unix_s": received_at_unix_s,
            }
        )

    @app.post("/api/v1/event")
    def create_event() -> Response:
        cfg = app.config["INKI_MONITOR_CONFIG"]
        token_expected = str(_cfg_get(cfg, ["auth", "ingest_token"], ""))
        token_got = _get_bearer_token()
        if not token_expected or token_got != token_expected:
            return jsonify({"ok": False, "error": "unauthorized"}), 401

        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"ok": False, "error": "invalid JSON"}), 400

        device_id = payload.get("device_id", "")
        if not isinstance(device_id, str) or not DEVICE_ID_RE.fullmatch(device_id):
            return jsonify({"ok": False, "error": "device_id missing or invalid"}), 400

        event_type = payload.get("event_type", "")
        if not isinstance(event_type, str) or not event_type:
            return jsonify({"ok": False, "error": "event_type required"}), 400

        event_payload = json.dumps(payload.get("payload", {}), separators=(",", ":"))
        author = payload.get("author")
        created_at = payload.get("created_at")
        if created_at is not None:
            try:
                created_at = int(created_at)
            except (TypeError, ValueError):
                return jsonify({"ok": False, "error": "created_at must be unix timestamp"}), 400

        event_id = db.insert_event(
            app.config["INKI_MONITOR_DB_PATH"],
            device_id,
            event_type,
            event_payload,
            author,
            created_at=created_at,
        )
        return jsonify({"ok": True, "event_id": event_id})

    @app.get("/api/v1/events/<device_id>")
    def list_events(device_id: str) -> Response:
        if not DEVICE_ID_RE.fullmatch(device_id):
            abort(404)
        event_type = request.args.get("type")
        since = request.args.get("since")
        since_int = int(since) if since and since.isdigit() else None
        rows = db.get_events(
            app.config["INKI_MONITOR_DB_PATH"], device_id, event_type, since_int
        )
        return jsonify(
            [
                {
                    "id": row["id"],
                    "device_id": row["device_id"],
                    "created_at": row["created_at"],
                    "event_type": row["event_type"],
                    "payload": json.loads(row["payload"]),
                    "author": row["author"],
                }
                for row in rows
            ]
        )

    @app.delete("/api/v1/event/<int:event_id>")
    def remove_event(event_id: int) -> Response:
        cfg = app.config["INKI_MONITOR_CONFIG"]
        token_expected = str(_cfg_get(cfg, ["auth", "ingest_token"], ""))
        token_got = _get_bearer_token()
        if not token_expected or token_got != token_expected:
            return jsonify({"ok": False, "error": "unauthorized"}), 401

        deleted = db.delete_event(app.config["INKI_MONITOR_DB_PATH"], event_id)
        if not deleted:
            return jsonify({"ok": False, "error": "not found"}), 404
        return jsonify({"ok": True})

    @app.get("/")
    def index() -> str:
        db_path = app.config["INKI_MONITOR_DB_PATH"]
        rows = db.list_devices(db_path)
        lifecycle = db.get_latest_lifecycle_events(db_path)
        now_unix_s = int(time.time())
        stale_hours_default = int(_cfg_get(config, ["ui", "stale_threshold_hours"], 2))
        stale_hours = _safe_int(request.args.get("stale_hours"), stale_hours_default)
        if stale_hours < 1:
            stale_hours = stale_hours_default
        stale_threshold_s = stale_hours * 3600

        status_order = {"active": 0, "stale": 1, "relocated": 2, "retired": 3}
        devices = []
        for row in rows:
            item = dict(row)
            item["last_seen_ago"] = _format_last_seen_ago(item.get("last_seen_unix_s"), now_unix_s)

            lc = lifecycle.get(item["device_id"])
            if lc and lc["event_type"] == "retired":
                item["status"] = "retired"
            elif lc and lc["event_type"] == "relocated":
                item["status"] = "relocated"
            elif item.get("last_seen_unix_s") and (now_unix_s - int(item["last_seen_unix_s"])) > stale_threshold_s:
                item["status"] = "stale"
            else:
                item["status"] = "active"
            devices.append(item)

        devices.sort(key=lambda d: (status_order.get(d["status"], 9), d.get("label") or d["device_id"]))

        return render_template(
            "index.html",
            devices=devices,
            stale_threshold_hours=stale_hours,
            title=_cfg_get(config, ["ui", "title"], "inki-monitor"),
            repo_info=app.config["INKI_MONITOR_REPO_INFO"],
        )

    @app.get("/device/<device_id>")
    def device_page(device_id: str) -> str:
        if not DEVICE_ID_RE.fullmatch(device_id):
            abort(404)
        device = db.get_device(app.config["INKI_MONITOR_DB_PATH"], device_id)
        if device is None:
            abort(404)

        window = request.args.get("window", "battery")
        l_param = request.args.get("l", "0")
        s_param = request.args.get("s", "0")
        show_battery_post = _query_flag("show_battery_post")
        show_battery_sag = _query_flag("show_battery_sag")
        show_coin_cell = _query_flag("show_coin_cell")
        show_time_to_telemetry = _query_flag("show_time_to_telemetry")
        show_pico_temp = _query_flag("show_pico_temp")
        show_query_ok = _query_flag("show_query_ok")
        show_wifi_rssi = _query_flag("show_wifi_rssi")
        show_wake_source = _query_flag("show_wake_source")
        show_events = _query_flag("show_events")
        ingest_token = str(_cfg_get(config, ["auth", "ingest_token"], ""))
        return render_template(
            "device.html",
            device=device,
            window=window,
            l_param=l_param,
            s_param=s_param,
            show_battery_post=show_battery_post,
            show_battery_sag=show_battery_sag,
            show_coin_cell=show_coin_cell,
            show_time_to_telemetry=show_time_to_telemetry,
            show_pico_temp=show_pico_temp,
            show_query_ok=show_query_ok,
            show_wifi_rssi=show_wifi_rssi,
            show_wake_source=show_wake_source,
            show_events=show_events,
            title=_cfg_get(config, ["ui", "title"], "inki-monitor"),
            repo_info=app.config["INKI_MONITOR_REPO_INFO"],
            ingest_token=ingest_token,
        )

    @app.get("/api/v1/device/<device_id>/plot-data")
    def device_plot_data(device_id: str) -> Response:
        if not DEVICE_ID_RE.fullmatch(device_id):
            abort(404)
        db_path = app.config["INKI_MONITOR_DB_PATH"]
        device = db.get_device(db_path, device_id)
        if device is None:
            abort(404)

        window = request.args.get("window", "")
        now_unix_s = int(time.time())
        since: int | None = None
        window_label = "all"

        if window == "battery":
            ev = db.get_latest_event(db_path, device_id, "battery_change")
            if ev:
                since = int(ev["created_at"])
                window_label = "battery"
            else:
                window_label = "all"
        elif window.endswith("h") and window[:-1].isdigit():
            since = now_unix_s - int(window[:-1]) * 3600
            window_label = window
        elif window.endswith("d") and window[:-1].isdigit():
            since = now_unix_s - int(window[:-1]) * 86400
            window_label = window
        elif window == "all" or window == "":
            since = None
            window_label = "all"

        # Legacy l/s support (only if no window param)
        l_param = request.args.get("l", "0")
        s_param = request.args.get("s", "0")
        use_legacy = (not window) and (l_param != "0" or s_param != "0")

        if use_legacy:
            rows = db.get_device_samples(db_path, device_id)
            window_rows, start_idx, end_idx = _apply_legacy_window(rows, l_param, s_param)
        else:
            window_rows = db.get_device_samples(db_path, device_id, since=since)

        return jsonify(
            {
                "device_id": device_id,
                "label": device["label"],
                "window": window_label,
                "count": len(window_rows),
                "samples": [_plot_sample_row_to_dict(row) for row in window_rows],
            }
        )

    return app


def main() -> None:
    app = create_app()
    cfg = app.config["INKI_MONITOR_CONFIG"]
    bind_host = str(_cfg_get(cfg, ["server", "bind_host"], "0.0.0.0"))
    bind_port = int(_cfg_get(cfg, ["server", "bind_port"], 3004))
    app.run(host=bind_host, port=bind_port, debug=False)


if __name__ == "__main__":
    main()
