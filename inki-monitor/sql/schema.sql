PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    label TEXT,
    first_seen_unix_s INTEGER NOT NULL,
    last_seen_unix_s INTEGER NOT NULL,
    last_use_case TEXT,
    last_room_type TEXT,
    last_rtc_backend TEXT,
    last_fw_version TEXT,
    last_fw_build_date TEXT,
    last_query_ok INTEGER,
    last_wifi_rssi_dbm REAL,
    last_telemetry_send_elapsed_ms INTEGER,
    last_battery_before_wifi_v REAL,
    last_battery_after_wifi_v REAL,
    last_battery_sag_v REAL,
    last_coin_cell_v REAL,
    last_pico_temp_c REAL,
    updated_at_unix_s INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    received_at_unix_s INTEGER NOT NULL,
    label TEXT,
    use_case TEXT,
    room_type TEXT,
    rtc_backend TEXT,
    fw_version TEXT,
    fw_build_date TEXT,
    query_ok INTEGER,
    wifi_rssi_dbm REAL,
    telemetry_send_elapsed_ms INTEGER,
    battery_before_wifi_v REAL,
    battery_after_wifi_v REAL,
    battery_sag_v REAL,
    coin_cell_v REAL,
    pico_temp_c REAL,
    payload_json TEXT NOT NULL,
    remote_addr TEXT
);

CREATE INDEX IF NOT EXISTS idx_samples_device_time
    ON samples(device_id, received_at_unix_s);

CREATE INDEX IF NOT EXISTS idx_samples_time
    ON samples(received_at_unix_s);

CREATE INDEX IF NOT EXISTS idx_devices_label
    ON devices(label);
