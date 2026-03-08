from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any


SchemaSample = dict[str, Any]


def _connect(db_path: str | Path) -> sqlite3.Connection:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    return conn


def _table_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    rows = conn.execute(f"PRAGMA table_info({table})").fetchall()
    return {str(row["name"]) for row in rows}


def _assert_required_schema_columns(conn: sqlite3.Connection) -> None:
    required_samples = {
        "device_id",
        "received_at_unix_s",
        "telemetry_send_elapsed_ms",
        "battery_before_wifi_v",
        "battery_after_wifi_v",
        "battery_sag_v",
        "coin_cell_v",
        "pico_temp_c",
        "payload_json",
        "fw_build_date",
    }
    required_devices = {
        "device_id",
        "last_fw_build_date",
        "last_telemetry_send_elapsed_ms",
        "last_battery_before_wifi_v",
        "last_battery_after_wifi_v",
        "last_battery_sag_v",
        "last_coin_cell_v",
        "last_pico_temp_c",
    }

    samples_cols = _table_columns(conn, "samples")
    devices_cols = _table_columns(conn, "devices")

    missing_samples = sorted(required_samples - samples_cols)
    missing_devices = sorted(required_devices - devices_cols)
    if missing_samples or missing_devices:
        details: list[str] = []
        if missing_samples:
            details.append(f"samples missing: {', '.join(missing_samples)}")
        if missing_devices:
            details.append(f"devices missing: {', '.join(missing_devices)}")
        raise RuntimeError(
            "SQLite schema mismatch (old inki-monitor DB). "
            "Delete the DB file and restart. " + " | ".join(details)
        )


def _ensure_additive_columns(conn: sqlite3.Connection) -> None:
    samples_cols = _table_columns(conn, "samples")
    devices_cols = _table_columns(conn, "devices")

    if "fw_build_date" not in samples_cols:
        conn.execute("ALTER TABLE samples ADD COLUMN fw_build_date TEXT")
    if "last_fw_build_date" not in devices_cols:
        conn.execute("ALTER TABLE devices ADD COLUMN last_fw_build_date TEXT")


def init_db(db_path: str | Path, schema_path: str | Path) -> None:
    db_path = Path(db_path)
    db_path.parent.mkdir(parents=True, exist_ok=True)

    schema_sql = Path(schema_path).read_text(encoding="utf-8")
    with _connect(db_path) as conn:
        conn.executescript(schema_sql)
        _ensure_additive_columns(conn)
        _assert_required_schema_columns(conn)


def insert_sample_and_update_device(
    db_path: str | Path,
    sample: SchemaSample,
    payload_json: str,
    remote_addr: str | None,
) -> None:
    query_ok_db = None if sample.get("query_ok") is None else int(bool(sample["query_ok"]))

    with _connect(db_path) as conn:
        conn.execute(
            """
            INSERT INTO samples (
                device_id, received_at_unix_s, label, use_case, room_type,
                rtc_backend, fw_version, fw_build_date, query_ok, wifi_rssi_dbm, telemetry_send_elapsed_ms,
                battery_before_wifi_v, battery_after_wifi_v, battery_sag_v,
                coin_cell_v, pico_temp_c, payload_json, remote_addr
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                sample["device_id"],
                sample["received_at_unix_s"],
                sample.get("label"),
                sample.get("use_case"),
                sample.get("room_type"),
                sample.get("rtc_backend"),
                sample.get("fw_version"),
                sample.get("fw_build_date"),
                query_ok_db,
                sample.get("wifi_rssi_dbm"),
                sample.get("telemetry_send_elapsed_ms"),
                sample.get("battery_before_wifi_v"),
                sample.get("battery_after_wifi_v"),
                sample.get("battery_sag_v"),
                sample.get("coin_cell_v"),
                sample.get("pico_temp_c"),
                payload_json,
                remote_addr,
            ),
        )

        conn.execute(
            """
            INSERT INTO devices (
                device_id, label, first_seen_unix_s, last_seen_unix_s,
                last_use_case, last_room_type, last_rtc_backend,
                last_fw_version, last_fw_build_date, last_query_ok, last_wifi_rssi_dbm, last_telemetry_send_elapsed_ms,
                last_battery_before_wifi_v, last_battery_after_wifi_v, last_battery_sag_v,
                last_coin_cell_v, last_pico_temp_c, updated_at_unix_s
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                label = excluded.label,
                last_seen_unix_s = excluded.last_seen_unix_s,
                last_use_case = excluded.last_use_case,
                last_room_type = excluded.last_room_type,
                last_rtc_backend = excluded.last_rtc_backend,
                last_fw_version = excluded.last_fw_version,
                last_fw_build_date = excluded.last_fw_build_date,
                last_query_ok = excluded.last_query_ok,
                last_wifi_rssi_dbm = excluded.last_wifi_rssi_dbm,
                last_telemetry_send_elapsed_ms = excluded.last_telemetry_send_elapsed_ms,
                last_battery_before_wifi_v = excluded.last_battery_before_wifi_v,
                last_battery_after_wifi_v = excluded.last_battery_after_wifi_v,
                last_battery_sag_v = excluded.last_battery_sag_v,
                last_coin_cell_v = excluded.last_coin_cell_v,
                last_pico_temp_c = excluded.last_pico_temp_c,
                updated_at_unix_s = excluded.updated_at_unix_s
            """,
            (
                sample["device_id"],
                sample.get("label"),
                sample["received_at_unix_s"],
                sample["received_at_unix_s"],
                sample.get("use_case"),
                sample.get("room_type"),
                sample.get("rtc_backend"),
                sample.get("fw_version"),
                sample.get("fw_build_date"),
                query_ok_db,
                sample.get("wifi_rssi_dbm"),
                sample.get("telemetry_send_elapsed_ms"),
                sample.get("battery_before_wifi_v"),
                sample.get("battery_after_wifi_v"),
                sample.get("battery_sag_v"),
                sample.get("coin_cell_v"),
                sample.get("pico_temp_c"),
                sample["received_at_unix_s"],
            ),
        )


def list_devices(db_path: str | Path) -> list[sqlite3.Row]:
    with _connect(db_path) as conn:
        rows = conn.execute(
            """
            SELECT *
            FROM devices
            ORDER BY
                CASE WHEN label IS NULL OR label = '' THEN 1 ELSE 0 END,
                lower(COALESCE(label, '')),
                lower(device_id)
            """
        ).fetchall()
    return rows


def get_device(db_path: str | Path, device_id: str) -> sqlite3.Row | None:
    with _connect(db_path) as conn:
        row = conn.execute(
            "SELECT * FROM devices WHERE device_id = ?",
            (device_id,),
        ).fetchone()
    return row


def get_device_samples(
    db_path: str | Path, device_id: str, since: int | None = None
) -> list[sqlite3.Row]:
    if since is not None:
        query = """
            SELECT * FROM samples
            WHERE device_id = ? AND received_at_unix_s >= ?
            ORDER BY received_at_unix_s ASC, id ASC
        """
        params: tuple = (device_id, since)
    else:
        query = """
            SELECT * FROM samples
            WHERE device_id = ?
            ORDER BY received_at_unix_s ASC, id ASC
        """
        params = (device_id,)
    with _connect(db_path) as conn:
        rows = conn.execute(query, params).fetchall()
    return rows


def insert_event(
    db_path: str | Path,
    device_id: str,
    event_type: str,
    payload: str = "{}",
    author: str | None = None,
    created_at: int | None = None,
) -> int:
    import time as _time

    ts = created_at if created_at is not None else int(_time.time())
    with _connect(db_path) as conn:
        cur = conn.execute(
            """
            INSERT INTO events (device_id, created_at, event_type, payload, author)
            VALUES (?, ?, ?, ?, ?)
            """,
            (device_id, ts, event_type, payload, author),
        )
        return cur.lastrowid  # type: ignore[return-value]


def get_events(
    db_path: str | Path,
    device_id: str,
    event_type: str | None = None,
    since: int | None = None,
) -> list[sqlite3.Row]:
    clauses = ["device_id = ?"]
    params: list[Any] = [device_id]
    if event_type is not None:
        clauses.append("event_type = ?")
        params.append(event_type)
    if since is not None:
        clauses.append("created_at >= ?")
        params.append(since)
    where = " AND ".join(clauses)
    with _connect(db_path) as conn:
        rows = conn.execute(
            f"SELECT * FROM events WHERE {where} ORDER BY created_at ASC, id ASC",
            params,
        ).fetchall()
    return rows


def get_latest_event(
    db_path: str | Path, device_id: str, event_type: str
) -> sqlite3.Row | None:
    with _connect(db_path) as conn:
        row = conn.execute(
            """
            SELECT * FROM events
            WHERE device_id = ? AND event_type = ?
            ORDER BY created_at DESC LIMIT 1
            """,
            (device_id, event_type),
        ).fetchone()
    return row


def delete_event(db_path: str | Path, event_id: int) -> bool:
    with _connect(db_path) as conn:
        cur = conn.execute("DELETE FROM events WHERE id = ?", (event_id,))
        return cur.rowcount > 0


def get_latest_lifecycle_events(db_path: str | Path) -> dict[str, sqlite3.Row]:
    """Return the latest lifecycle event (retired/relocated) per device."""
    with _connect(db_path) as conn:
        rows = conn.execute(
            """
            SELECT e.*
            FROM events e
            INNER JOIN (
                SELECT device_id, MAX(created_at) AS max_ts
                FROM events
                WHERE event_type IN ('retired', 'relocated')
                GROUP BY device_id
            ) latest ON e.device_id = latest.device_id
                    AND e.created_at = latest.max_ts
                    AND e.event_type IN ('retired', 'relocated')
            """
        ).fetchall()
    return {row["device_id"]: row for row in rows}
